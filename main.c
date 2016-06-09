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

#define DEFAULT_DB_DIR "./examples" /* change later */
#define DEFAULT_PORT "50000"
#define VERSION "0.1"

#define LINE_MAX 1024

static const char *db_dir = DEFAULT_DB_DIR;
static const char *port = DEFAULT_PORT;

static __attribute__ ((unused)) void print_addrinfo(struct addrinfo *ai)
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
    if (ret != 0) {
        log_msg("getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

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
            log_errno("listen");
        else
            return sfd; /* Success */
    }

    return -1;
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

    if (open_db(db_dir) != 0)
        log_msg_die("Error opening database %s\n", db_dir);
    
    int listen_fd = create_listening_socket(port);
    if (listen_fd == -1)
        log_msg_die("Could not create listening socket on port %s\n", port);

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
        setlinebuf(peer_stream);

        send_reply_ok(peer_stream, "Etestd %s", VERSION);
        struct credentials peer_creds = { NULL, AUTH_LEVEL_UNAUTHORIZED };

        for (;;) {
            char line[LINE_MAX];
            fgets(line, LINE_MAX, peer_stream);
            if (handle_request(line, &peer_creds, peer_stream) != 0)
                break;
        }

        free(peer_creds.username);
        fclose(peer_stream);
    }

    close_db();
    close(listen_fd);
    
    return 0;
}
