#include <stdio.h>

enum {
    AUTH_LEVEL_UNAUTHORIZED =   0x1,
    AUTH_LEVEL_STUDENT =        0x2,
    AUTH_LEVEL_EXAMINER =       0x4,
    AUTH_LEVEL_ADMINISTRATOR =  0x8
};

enum {
    REQUEST_USER,
    REQUEST_GET_TEST,
    REQUEST_GET_TESTS,
    REQUEST_GET_USERS,
    REQUEST_GET_GROUPS,
    REQUEST_PUT_ANSWERS,
    REQUEST_PUT_TEST,
    REQUEST_PUT_USER,
    REQUEST_PUT_GROUPS,
    REQUEST_DELETE_TEST,
    REQUEST_DELETE_USER,
    REQUEST_DELETE_GROUP,
    REQUEST_BYE
};

enum {
    REPLY_OK,
    REPLY_ERR
};

struct credentials {
    char *username;
    int auth_level;
};

int send_reply_ok(FILE *stream, const char *format, ...);

int send_reply_err(FILE *stream, const char *format, ...);

int handle_request(struct credentials *peer_creds, FILE *peer_stream);
