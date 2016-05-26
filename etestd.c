#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/socket.h>
#include <json-c/json.h>
#include <uuid/uuid.h>

#include "log.h"

#define DEFAULT_DB_DIR "." /* change to /var/lib/etest (/srv/etest ?) later */
#define DEFAULT_PORT "50000"

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


/**
 * Wyślij do strumienia nagłówki testów. tests powinien zawierać
 * listę testów wczytaną z pliku z testami.
 *
 * @param tests lista testów wczytana z pliku z testami
 * @param fp strumień do zapisu
 *
 * @returns 0 w przypadku sukcesu. W przeciwnym wypadku -1.
 */  
int write_test_headers(struct json_object *tests, FILE *fp)
{
    if (!json_object_is_type(tests, json_type_array))
        return -1;
        
    for (int i = 0; i < json_object_array_length(tests); i++) {
        struct json_object *test = json_object_array_get_idx(tests, i);
        if (!json_object_is_type(test, json_type_object))
            return -1;
        json_object_object_del(test, "questions");
        json_object_object_del(test, "correctAnswers");
    }

    if (fputs(json_object_to_json_string_ext(tests, JSON_C_TO_STRING_PLAIN), fp) == EOF)
        return -1;
    return 0;
}

void print_addrinfo(struct addrinfo *ai)
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

int create_listening_socket(const char *port)
{
    struct addrinfo hints = {0};
    struct addrinfo *result, *rp;
    
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV | AI_V4MAPPED;

    int ret = getaddrinfo(NULL, port, &hints, &result);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    int sfd;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        /* print_addrinfo(rp); */
        sfd = socket(rp->ai_family, rp->ai_socktype,
                    rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0}, sizeof(int)) == -1)
            perror("setsockopt");

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sfd);
    }
    
    if (result != NULL) {
        freeaddrinfo(rp);
        if (listen(sfd, SOMAXCONN) == -1)
            perror("listen");
        else
            return sfd;
    } else
        fprintf(stderr, "Could not bind a socket\n");
        
    return -1;
}

struct json_object *get_test(uuid_t id, struct json_object *tests)
{
    if (!json_object_is_type(tests, json_type_array))
        return NULL;
        
    for (int i = 0; i < json_object_array_length(tests); i++) {
        struct json_object *test = json_object_array_get_idx(tests, i);
        json_object *subobj;
        if (json_object_object_get_ex(test, "id", &subobj) == TRUE) {
            uuid_t id_in_json;
            if (uuid_parse(json_object_get_string(subobj), id_in_json) == 0)
                if (uuid_compare(id, id_in_json) == 0)
                    return test;
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    const char *db_dir = DEFAULT_DB_DIR;
    const char *port = DEFAULT_PORT;

    {
        int opt;
        static struct option long_options[] = {
            {"db-dir", required_argument, 0, 'd'},
            {"port", required_argument, 0, 'p'},
            {0, 0, 0, 0}
        };
        
        while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
            switch (opt) {
                case 'p':
                    port = optarg;
                    break;
                case 'd':
                    db_dir = optarg;
                    break;
                case '?':
                    fprintf(stderr, "Usage: %s [--db-dir DIR] [--port PORT]\n", argv[0]);
                    exit(EXIT_FAILURE);
                default:
                    abort();
            }
        }
    }
    
    char *tests_filename, *answers_filename,
         *users_filename, *groups_filename;
         
    asprintf(&tests_filename, "%s/%s", db_dir, "tests");
    asprintf(&answers_filename, "%s/%s", db_dir, "answers");
    asprintf(&users_filename, "%s/%s", db_dir, "users");
    asprintf(&groups_filename, "%s/%s", db_dir, "groups");

    free(tests_filename);
    free(answers_filename);
    free(users_filename);
    free(groups_filename);

    /* char *tests_json_string;
    if (etest_read_file(TESTS_FILE, &tests_json_string) != 0) {
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

    /* struct json_object *jobj = json_object_from_file(TESTS_FILE);
    if (etest_write_test_headers(jobj, stdout) != 0)
        puts("err");
    puts("\n");

    struct json_object *test;
    uuid_t id;
    uuid_parse("6fc51d84-b28b-4390-9c09-74ec4107ed00", id);
    test = etest_get_test(id, jobj);
    if (test)
        puts(json_object_to_json_string(test));
    
    int ret = json_object_put(jobj);
    printf("json_object_put(jobj) returned %i\n", ret); */

    int listen_fd = create_listening_socket(port);
    if (listen_fd == -1)
        logmsg_die("Could not create listening socket on port %s\n", port);
        
    return 0;
}
