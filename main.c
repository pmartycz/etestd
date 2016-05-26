#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/socket.h>
#include <string.h>
#include <json-c/json.h>
#include <uuid/uuid.h>

#include "common.h"
#include "db.h"
#include "protocol.h"

#define DEFAULT_DB_DIR "examples" /* change to /var/lib/etest (/srv/etest ?) later */
#define DEFAULT_PORT "50000"
#define VERSION "0.1"

static const char *db_dir = DEFAULT_DB_DIR;
static const char *port = DEFAULT_PORT;

/**
 * Wczytuje plik do dynamicznie alokowanego bufora.
 * Wywołujący jest odpowiedzialny za zwolnienie pamięci.
 *
 * @warning W przypadku niepowodzenia wskaźnik *ptr nie jest
 * modyfikowany i nie należy go zwalniać.
 * 
 * @param filename nazwa pliku
 * @param ptr [IN/OUT] adres wskaźnika pod którym zostanie zaalokowana pamięć
 *
 * @returns 0 w przypadku sukcesu. W przeciwnym wypadku -1.
 */ 
int read_file(const char *filename, char **ptr)
{
    FILE *fp = fopen(filename, "r");
    
    if (fp == NULL) {
        perror("fopen");
        return -1;
    }
    
    /* Go to the end of the file. */
    if (fseek(fp, 0L, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return -1;
    }
        
    /* Get the size of the file. */
    long bufsize = ftell(fp);
    if (bufsize == -1) {
        perror("ftell");
        fclose(fp);
        return -1;
    }

    /* Allocate our buffer to that size. */
    char *buf = malloc(sizeof(char) * (bufsize + 1));
    if (!buf) {
        perror("malloc");
        fclose(fp);
        return -1;
    }

    /* Go back to the start of the file. Read the entire file into memory. */
    if (fseek(fp, 0L, SEEK_SET) != 0 || fread(buf, sizeof(char), bufsize, fp) != bufsize) {
        free(buf);
        fclose(fp);
        return -1;
    }
    
    buf[bufsize] = '\0';
    *ptr = buf;
    fclose(fp);
    
    return 0;
}

static void print_addrinfo(struct addrinfo *ai)
{
    int res;
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    
    switch (ai->ai_family) {
        case AF_UNSPEC:     puts("AF_UNSPEC"); break;
        case AF_INET:       puts("AF_INET"); break;
        case AF_INET6:      puts("AF_INET6"); break;
        default:            puts("Unknown address family");
    }

    switch (ai->ai_socktype) {
        case SOCK_RAW:      puts("SOCK_RAW"); break;
        case SOCK_STREAM:   puts("SOCK_STREAM"); break;
        case SOCK_DGRAM:    puts("SOCK_DGRAM"); break;
        default:            puts("Unknown socket type");
    }

    switch (ai->ai_protocol) {
        case IPPROTO_RAW:   puts("IPPROTO_RAW"); break;
        case IPPROTO_TCP:   puts("IPPROTO_TCP"); break;
        case IPPROTO_UDP:   puts("IPPROTO_UDP"); break;
        default:            puts("Unknown protocol");
    }
    
    res = getnameinfo(ai->ai_addr, ai->ai_addrlen, hbuf, sizeof hbuf,
        sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV);
    if (res == 0)
        printf("host=%s service=%s\n\n", hbuf, sbuf);
    else
        fprintf(stderr, "getaddrinfo: %s\n\n", gai_strerror(res));
}

static int create_listening_socket(const char *port)
{
    struct addrinfo hints = {0};
    struct addrinfo *result, *rp;
    
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV | AI_V4MAPPED;

    int ret = getaddrinfo(NULL, port, &hints, &result);
    if (ret != 0)
        log_msg_die("getaddrinfo: %s\n", gai_strerror(ret));

    int sfd;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        /* print_addrinfo(rp); */
        sfd = socket(rp->ai_family, rp->ai_socktype,
                    rp->ai_protocol);
        if (sfd == -1) {
            log_errno("socket");
            continue;
        }

        if (setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0}, sizeof(int)) == -1)
            log_errno("setsockopt");

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        log_errno("bind");
        close(sfd);
    }
    
    freeaddrinfo(result);
    
    if (rp != NULL) {
        if (listen(sfd, SOMAXCONN) == -1)
            perror("listen");
        else
            return sfd; /* Success */
    }
    
    exit(EXIT_FAILURE);
}

static int accept_connection(int listen_fd)
{
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len = sizeof(struct sockaddr_storage);
    int peer_fd = accept(listen_fd, (struct sockaddr *) &peer_addr, &peer_addr_len);

    if (peer_fd == -1) {
        int ret;
        int saved_errno = errno;
        char host[NI_MAXHOST];
        ret = getnameinfo((struct sockaddr *) &peer_addr, peer_addr_len, host, NI_MAXHOST,
                            NULL, 0, NI_NUMERICHOST);
        if (ret == 0)
            log_msg("Failed to accept connection from %s: %s\n", host, strerror(saved_errno));
        else {
            log_msg("Failed to accept connection: %s\n", strerror(saved_errno));
        }
    }

    return peer_fd;
}

static void print_usage(char *arg0)
{
    fprintf(stderr, "Usage: %s [--db-dir DIR] [--port PORT] [-h|--help]\n", arg0);
}

static void print_help(char *arg0)
{
    /* Add short description */
    fprintf(stderr, "Etestd %s\n", VERSION);
    print_usage(arg0);
    /* ...and credits */
}

static void parse_args(int argc, char* argv[])
{
    int opt;
    enum {
        ARG_DB_DIR,
        ARG_PORT,
    };
    
    static struct option long_options[] = {
        {"db-dir", required_argument, 0, ARG_DB_DIR},
        {"port", required_argument, 0, ARG_PORT},
        {"help", required_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
            case ARG_PORT:
                port = optarg;
                break;
            case ARG_DB_DIR:
                db_dir = optarg;
                break;
            case 'h':
                print_help(argv[0]);
                exit(EXIT_SUCCESS);
            case '?':
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            default:
                abort();
        }
    }

    if (optind < argc) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    
    char *tests_filename, *answers_filename,
         *users_filename, *groups_filename;
         
    asprintf(&tests_filename, "%s/%s", db_dir, "tests");
    asprintf(&answers_filename, "%s/%s", db_dir, "answers");
    asprintf(&users_filename, "%s/%s", db_dir, "users");
    asprintf(&groups_filename, "%s/%s", db_dir, "groups");

    /* char *tests_json_string;
    if (read_file(TESTS_FILE, &tests_json_string) != 0) {
        fprintf(stderr, "Error reading tests file %s\n", TESTS_FILE);
        exit(EXIT_FAILURE);
    }

    json_object *jobj = json_tokener_parse(tests_json_string);
    puts(json_object_to_json_string(jobj));
    printf("json_object_put(jobj) returned %i\n", json_object_put(jobj));

    free(tests_json_string); */

    /* struct json_object_iterator it = json_object_iter_begin(test);
    struct json_object_iterator it_end = json_object_iter_end(json_object_array_get_idx(test));

    while (!json_object_iter_equal(&it, &it_end)) {
        printf("key: %s value: %s\n", json_object_iter_peek_name(&it),
            json_object_to_json_string(json_object_iter_peek_value(&it)));
        json_object_iter_next(&it);
    } */

    /* struct json_object *tests = json_object_from_file(tests_filename);
    if (write_test_headers(tests, stdout) != 0)
        puts("err");
    puts("\n");

    struct json_object *test;
    uuid_t id;
    uuid_parse("6fc51d84-b28b-4390-9c09-74ec4107ed00", id);
    test = get_test(id, tests);
    if (test)
        puts(json_object_to_json_string(test));
    
    int ret = json_object_put(tests);
    printf("json_object_put(tests) returned %i\n", ret);

    return 0; */

    int listen_fd = create_listening_socket(port);
    /* if (listen_fd == -1)
        log_msg_die("Could not create listening socket on port %s\n", port); */

    for (;;) {
        int peer_fd = accept_connection(listen_fd);
        if (peer_fd == -1)
            continue;

        FILE *peer_stream = fdopen(peer_fd, "r+");
        if (!peer_stream) {
            log_errno("Could not associate stream with fd");
            close(peer_fd);
            continue;
        }

        auth_level peer_auth_level = auth_level_unauthorized;

        fprintf(peer_stream, "+OK Etestd %s\r\n", VERSION);
        // send_reply(peer_stream, reply_type_ok, "Etestd %s", VERSION);
        fclose(peer_stream);
    }

    close(listen_fd);
    free(tests_filename);
    free(answers_filename);
    free(users_filename);
    free(groups_filename);
    
    return 0;
}
