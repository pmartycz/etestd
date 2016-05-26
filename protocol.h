#include <stdio.h>

typedef enum {
    auth_level_unauthorized,
    auth_level_student,
    auth_level_examinator,
    auth_level_administrator
} auth_level;

typedef enum {
    reply_type_ok,
    reply_type_err
} reply_type;

struct arg_required_auth_level {
        const char *name;
        auth_level required_auth_level;
};

static const struct {
    const char *method;
    struct arg_required_auth_level *args;
} auth_table[] = {
    {
        "USER",
        (struct arg_required_auth_level[]) {
            {"", auth_level_unauthorized}, /* no arg */
            {NULL, 0}
        }
    },
    {
        "GET",
        (struct arg_required_auth_level[]) {
            {"TESTS", auth_level_student},
            {"TEST", auth_level_student},
            {"USERS", auth_level_student},
            {"GROUPS", auth_level_examinator},
            {NULL, 0}
        }
    },
    {
        "PUT",
        (struct arg_required_auth_level[]) {
            {"ANSWERS", auth_level_student},
            {"TEST", auth_level_examinator},
            {"USER", auth_level_administrator},
            {"GROUP", auth_level_administrator},
            {NULL, 0}
        }
    },
    {
        "DELETE",
        (struct arg_required_auth_level[]) {
            {"TEST", auth_level_student},
            {"USER", auth_level_student},
            {"GROUP", auth_level_student},
            {NULL, 0}
        }
    },
    {
        "BYE",
        (struct arg_required_auth_level[]) {
            {"", auth_level_unauthorized}, /* no arg */
            {NULL, 0}
        }
    },
    {
        NULL,
        NULL
    }
};

int send_reply(FILE *stream, reply_type type, const char *msg_fmt, ...);
/* check_auth(const char *method, const char *arg, );
 * check_auth(method_type method, method_argument_type arg, auth_level_type al); */
