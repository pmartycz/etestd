#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <json-c/json.h>

#include "common.h"
#include "protocol.h"
#include "db.h"

int has_required_auth_level(int required_auth_level, int peer_auth_level)
{
    return required_auth_level & peer_auth_level;
}

static int send_reply(FILE *stream, int reply_type, const char *format, va_list ap)
{
    const char *sig;
    
    if (reply_type == REPLY_OK)
        sig = "+OK ";
    else if (reply_type == REPLY_ERR)
        sig = "-ERR ";
    else
        return -EINVAL;

    fputs(sig, stream);
        
    vfprintf(stream, format, ap);

    fputs("\r\n", stream);

    if (ferror(stream))
        return -EIO;
    return 0;
}

int send_reply_ok(FILE *stream, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    int ret = send_reply(stream, REPLY_OK, format, ap);
    va_end(ap);

    return ret;
}

int send_reply_err(FILE *stream, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    int ret = send_reply(stream, REPLY_ERR, format, ap);
    va_end(ap);

    return ret;
}

struct request_info {
    int code;
    int required_auth_level;
};

static const struct method_map {
    const char *method;
    const char *const *args;
    const struct request_info *ri;
} method_map[] = {
    {
        "USER",
        NULL,
        (struct request_info []) {
            { REQUEST_USER,         AUTH_LEVEL_UNAUTHORIZED }
        }
    },
    {
        "GET",
        (const char *[]) { "TEST", "TESTS", "USERS", "GROUPS", NULL },
        (struct request_info []) {
            { REQUEST_GET_TEST,     AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINER },
            { REQUEST_GET_TESTS,    AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINER },
            { REQUEST_GET_USERS,    AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINER | AUTH_LEVEL_ADMINISTRATOR },
            { REQUEST_GET_GROUPS,   AUTH_LEVEL_EXAMINER | AUTH_LEVEL_ADMINISTRATOR }
        }
    },
    {
        "PUT",
        (const char *[]) { "ANSWERS", "TEST", "USER", "GROUP", NULL }, /* {TEST|USER|GROUP}S ? */
        (struct request_info []) {
            { REQUEST_PUT_ANSWERS,  AUTH_LEVEL_STUDENT },
            { REQUEST_PUT_TEST,     AUTH_LEVEL_EXAMINER },
            { REQUEST_PUT_USER,     AUTH_LEVEL_ADMINISTRATOR },
            { REQUEST_PUT_GROUP,    AUTH_LEVEL_ADMINISTRATOR }
        }
    },
    {
        "DELETE",
        (const char *[]) { "TEST", "USER", "GROUP", NULL }, /* {TEST|USER|GROUP}S ? */
        (struct request_info []) {
            { REQUEST_DELETE_TEST,  AUTH_LEVEL_EXAMINER | AUTH_LEVEL_ADMINISTRATOR },
            { REQUEST_DELETE_USER,  AUTH_LEVEL_ADMINISTRATOR },
            { REQUEST_DELETE_GROUP, AUTH_LEVEL_ADMINISTRATOR }
        }
    },
    {
        "BYE",
        NULL,
        (struct request_info []) {
            { REQUEST_BYE,          AUTH_LEVEL_UNAUTHORIZED | AUTH_LEVEL_STUDENT | \
                AUTH_LEVEL_EXAMINER | AUTH_LEVEL_ADMINISTRATOR }
        }
    },
    {
        NULL,
        NULL,
        NULL
    }
};

/* @warning Modifies request_line */
static const struct request_info *parse_request(char *request_line, char **line_ptr)
{
    char *first_token = strtok_r(request_line, " \r\n", line_ptr);
    if (!first_token) /* empty request, etc */
        return NULL;

    /* method without arg */
    for (const struct method_map *m = method_map; m->method != NULL; m++)
        if (m->args == NULL && strcasecmp(first_token, m->method) == 0)
            return m->ri;
            
    char *second_token = strtok_r(NULL, " \r\n", line_ptr);
    if (!second_token)
        return NULL;
    
    /* method with arg */
    for (const struct method_map *m = method_map; m->method != NULL; m++)
        if (m->args != NULL && strcasecmp(first_token, m->method) == 0)
            for (size_t i = 0; m->args[i] != NULL; i++)
                if (strcasecmp(second_token, m->args[i]) == 0)
                    return &m->ri[i];
    
    return NULL;
}

int auth(const char *username, struct credentials *peer_creds)
{
    json_object *users = get_users(); 
    if (!entity_exists(username, users))
        return -1; /* user doesn't exist */

    /* TODO authentication */

    peer_creds->username = strdup(username);
    
    json_object_put(users);

    /* authorization */
    
    json_object *groups = get_groups();

    if (user_is_administrator(username, groups))
        peer_creds->auth_level = AUTH_LEVEL_ADMINISTRATOR;
    else if (user_is_examiner(username, groups))
        peer_creds->auth_level = AUTH_LEVEL_EXAMINER;
    else
        peer_creds->auth_level = AUTH_LEVEL_STUDENT;

    json_object_put(groups);
        
    return 0;
}

static const char *auth_level_to_string(int auth_level)
{
    switch (auth_level) {
        case AUTH_LEVEL_ADMINISTRATOR:
            return "ADMINISTRATOR";
            break;
        case AUTH_LEVEL_EXAMINER:
            return "EXAMINER";
            break;
        case AUTH_LEVEL_STUDENT:
            return "STUDENT";
            break;
        case AUTH_LEVEL_UNAUTHORIZED:
            return "UNAUTHORIZED";
            break;
        default:
            return "UNKNOWN";
    }
    
}

int handle_request(char *request_line, FILE *peer_stream, struct credentials *peer_creds)
{
    char *line_ptr;
    
    const struct request_info *req_info = parse_request(request_line, &line_ptr);
    if (!req_info) {
        send_reply_err(peer_stream, "invalid request");
        return -1;
    }
        
    if (!has_required_auth_level(req_info->required_auth_level, peer_creds->auth_level)) {
        send_reply_err(peer_stream, "not authorized");
        return -1;
    }
    
    switch (req_info->code) {
        case REQUEST_USER: {
            char *username = strtok_r(NULL, " \r\n", &line_ptr);
            if (!username) {
                send_reply_err(peer_stream, "invalid request");
                return -1;
            }
            if (auth(username, peer_creds) == 0) {
                send_reply_ok(peer_stream, "%s Hello %s, how are you?",
                    auth_level_to_string(peer_creds->auth_level), username);
            } else {
                send_reply_err(peer_stream, "auth error");
                return -1;
            }
            break;
        }
        
        case REQUEST_GET_TESTS: {
            json_object *user_tests;
            
            switch (peer_creds->auth_level) {
                case AUTH_LEVEL_EXAMINER:
                    user_tests = get_tests_for_examiner(peer_creds->username);
                    break;
                case AUTH_LEVEL_STUDENT: {
                    user_tests = get_tests_for_student(peer_creds->username);
                    break;
                }
                default:
                    abort();
            }

            json_object *headers = get_test_headers(user_tests);
            
            send_reply_ok(peer_stream, "");
            fputs(json_object_to_json_string_ext(headers, JSON_C_TO_STRING_PLAIN), peer_stream);
            fputs("\r\n.\r\n", peer_stream);
            
            json_object_put(user_tests);
            break;
        }

        case REQUEST_GET_TEST:
        case REQUEST_GET_USERS:
        case REQUEST_GET_GROUPS:
        case REQUEST_PUT_ANSWERS:
        case REQUEST_PUT_TEST:
        case REQUEST_PUT_USER:
        case REQUEST_PUT_GROUP:
        case REQUEST_DELETE_TEST:
        case REQUEST_DELETE_USER:
        case REQUEST_DELETE_GROUP:
            send_reply_err(peer_stream, "not implemented");
            return 0;
        case REQUEST_BYE:
            send_reply_ok(peer_stream, "bye-bye");
            return -1;
        default:
            abort();
    }

    return 0;
}
