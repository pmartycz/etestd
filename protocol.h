#include <stdio.h>

enum auth_level {
    AUTH_LEVEL_UNAUTHORIZED =   0x1,
    AUTH_LEVEL_STUDENT =        0x2,
    AUTH_LEVEL_EXAMINATOR =     0x4,
    AUTH_LEVEL_ADMINISTRATOR =  0x8
};

/* These serve as indexes to request_required_auth_levels[]
 * Do not change order! */
enum request {
    REQUEST_INVALID,
    REQUEST_USER,
    REQUEST_GET_TEST,
    REQUEST_GET_TESTS,
    REQUEST_GET_USERS,
    REQUEST_GET_GROUPS,
    REQUEST_PUT_ANSWERS,
    REQUEST_PUT_TEST,
    REQUEST_PUT_USER,
    REQUEST_PUT_GROUP,
    REQUEST_DELETE_TEST,
    REQUEST_DELETE_USER,
    REQUEST_DELETE_GROUP,
    REQUEST_BYE
};

enum reply {
    REPLY_OK,
    REPLY_ERR
};

int has_required_auth_level(int request_type, int client_auth_level);

int send_reply_ok(FILE *stream, const char *format, ...);

int send_reply_err(FILE *stream, const char *format, ...);

int parse_request(char *request_line);
