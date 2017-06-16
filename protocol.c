/**
 * @file
 * @author Piotr Martycz <pmartycz@gmail.com>
 *
 * @section DESCRIPTION
 * Module implementing protocol for communication with client.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <json-c/json.h>
#include <nettle/md5.h>
#include <nettle/base16.h>

#include "common.h"
#include "protocol.h"
#include "db.h"

#define JSON_FLAGS  JSON_C_TO_STRING_PLAIN
#define LINE_LEN    1024
#define NO_AUTH     0

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

int send_data(FILE *stream, const char *string)
{
    fputs(string, stream);
    //fputs("\r\n.\r\n", stream);
    fputs("\r\n", stream);
    
    if (ferror(stream))
        return -EIO;
    return 0;
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
            { REQUEST_GET_TEST,     AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINER | AUTH_LEVEL_ADMINISTRATOR },
            { REQUEST_GET_TESTS,    AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINER | AUTH_LEVEL_ADMINISTRATOR },
            { REQUEST_GET_USERS,    AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINER | AUTH_LEVEL_ADMINISTRATOR },
            { REQUEST_GET_GROUPS,   AUTH_LEVEL_EXAMINER | AUTH_LEVEL_ADMINISTRATOR }
        }
    },
    {
        "PUT",
        (const char *[]) { "ANSWERS", "TEST", "USER", "GROUPS", NULL },
        (struct request_info []) {
            { REQUEST_PUT_ANSWERS,  AUTH_LEVEL_STUDENT },
            { REQUEST_PUT_TEST,     AUTH_LEVEL_EXAMINER },
            { REQUEST_PUT_USER,     AUTH_LEVEL_ADMINISTRATOR },
            { REQUEST_PUT_GROUPS,    AUTH_LEVEL_ADMINISTRATOR }
        }
    },
    {
        "DELETE",
        (const char *[]) { "TEST", "USER", "GROUP", NULL },
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

int authenticate_user(const char *username, const char *password_hash, FILE *peer_stream)
{
#if NO_AUTH
    return 0;
#endif
    uuid_t nonce;
    uuid_generate_random(nonce);
    char nonce_s[37];
    uuid_unparse(nonce, nonce_s);

    send_reply_ok(peer_stream, "%s", nonce_s);

    struct md5_ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, strlen(password_hash), (uint8_t *) password_hash);
    md5_update(&ctx, strlen(nonce_s), (uint8_t *) nonce_s);
    uint8_t my_digest[MD5_DIGEST_SIZE];
    md5_digest(&ctx, MD5_DIGEST_SIZE, my_digest);

    char line[LINE_LEN];
    if (fgets(line, LINE_LEN, peer_stream) == NULL)
        return -1;
    line[strcspn(line, "\r\n")] = '\0';

    if (strlen(line) != BASE16_ENCODE_LENGTH(MD5_DIGEST_SIZE))
        return -1;

    struct base16_decode_ctx decode_ctx;
    base16_decode_init(&decode_ctx);
    size_t dst_length;
    uint8_t peer_digest[MD5_DIGEST_SIZE];
    if (base16_decode_update(&decode_ctx, &dst_length, peer_digest,
        BASE16_ENCODE_LENGTH(MD5_DIGEST_SIZE), (uint8_t *) line) && base16_decode_final(&decode_ctx))
        if (memcmp(my_digest, peer_digest, MD5_DIGEST_SIZE) == 0)
            return 0;
            
    return -1;
}

void authorize_user(const char *username, struct credentials *peer_creds)
{
    peer_creds->username = strdup(username);

    json_object *groups = get_groups();

    if (user_is_administrator(username, groups))
        peer_creds->auth_level = AUTH_LEVEL_ADMINISTRATOR;
    else if (user_is_examiner(username, groups))
        peer_creds->auth_level = AUTH_LEVEL_EXAMINER;
    else
        peer_creds->auth_level = AUTH_LEVEL_STUDENT;

    json_object_put(groups);
}

int handle_request_user(const char *username, struct credentials *peer_creds, FILE *peer_stream)
{
    json_object *users = get_users();
    json_object *user = get_entity(username, users);
    json_object *password_hash = NULL;

    int ret;
    if (user &&
        json_object_object_get_ex(user, "passwordHash", &password_hash) == TRUE &&
        json_object_is_type(password_hash, json_type_string) &&
        authenticate_user(username, json_object_get_string(password_hash), peer_stream) == 0) {
            authorize_user(username, peer_creds);
            send_reply_ok(peer_stream, "%s Hello %s, how are you?", auth_level_to_string(peer_creds->auth_level), username);
            ret = 0;
    } else {
            send_reply_err(peer_stream, "auth error");
            ret = -1;
    }
    
    json_object_put(users);
            
    return ret;
}

json_object *get_tests_for_user(const struct credentials *peer_creds)
{
    switch (peer_creds->auth_level) {
        case AUTH_LEVEL_ADMINISTRATOR:
            return get_tests();
        case AUTH_LEVEL_EXAMINER:
            return get_tests_for_examiner(peer_creds->username);
        case AUTH_LEVEL_STUDENT:
            return get_tests_for_student(peer_creds->username);
        default:
            abort();
    }
}

json_object *get_test_for_user(uuid_t id, const struct credentials *peer_creds, json_object *tests)
{
    switch (peer_creds->auth_level) {
        case AUTH_LEVEL_ADMINISTRATOR:
        case AUTH_LEVEL_EXAMINER:
            return get_test(id, tests);
        case AUTH_LEVEL_STUDENT:
            return get_test_for_student(id, peer_creds->username, tests);
        default:
            abort();
    }
}

int handle_request_get_tests(const struct credentials *peer_creds, FILE *peer_stream)
{
    json_object *tests = get_tests_for_user(peer_creds); 
    
    remove_qa_from_tests(tests);
    
    send_reply_ok(peer_stream, "");
    send_data(peer_stream, json_object_to_json_string_ext(tests, JSON_FLAGS));
    
    json_object_put(tests);
    return 0;
}

int handle_request_get_test(uuid_t id, const struct credentials *peer_creds, FILE *peer_stream)
{
    int ret = 0;
    json_object *tests = get_tests_for_user(peer_creds);
    json_object *test = get_test_for_user(id, peer_creds, tests);

    if (test) {
        send_reply_ok(peer_stream, "");
        send_data(peer_stream, json_object_to_json_string_ext(test, JSON_FLAGS));
    } else {
        send_reply_err(peer_stream, "not available");
        ret = -1;
    }

    json_object_put(tests);
    return ret;
}

int handle_request_get_users(const struct credentials *peer_creds, FILE *peer_stream)
{
    json_object *users = NULL;
    
    switch (peer_creds->auth_level) {
        case AUTH_LEVEL_ADMINISTRATOR:
        case AUTH_LEVEL_EXAMINER:
        case AUTH_LEVEL_STUDENT:
            /* TODO students should not receive all users */
            users = get_users();
            break;
        default:
            abort();
    }

    /* Remove password hashes */
    if (json_object_is_type(users, json_type_array))
        for (int i = 0; i < json_object_array_length(users); i++) {
            json_object *user = json_object_array_get_idx(users, i);
            if (json_object_is_type(user, json_type_object))
                json_object_object_del(user, "passwordHash");
        }

    send_reply_ok(peer_stream, "");
    send_data(peer_stream, json_object_to_json_string_ext(users, JSON_FLAGS));

    json_object_put(users);
    return 0;
    
}

int handle_request_get_groups(const struct credentials *peer_creds, FILE *peer_stream)
{
    json_object *groups = NULL;

    switch (peer_creds->auth_level) {
        case AUTH_LEVEL_ADMINISTRATOR:
        case AUTH_LEVEL_EXAMINER:
            groups = get_groups();
            break;
        default:
            abort();
    }

    send_reply_ok(peer_stream, "");
    send_data(peer_stream, json_object_to_json_string_ext(groups, JSON_FLAGS));

    json_object_put(groups);
    return 0;
}

json_object *parse_json(FILE *peer_stream)
{
    char line[LINE_LEN];
    json_object *obj = NULL;
    json_tokener *tok = json_tokener_new();
    
    do {
        if (fgets(line, LINE_LEN, peer_stream) == NULL)
            break;
        obj = json_tokener_parse_ex(tok, line, strlen(line));
    } while (json_tokener_get_error(tok) == json_tokener_continue);
    
    json_tokener_free(tok);
    
    return obj;
}

int handle_request_put_answers(uuid_t id, const char *username, FILE *peer_stream)
{
    send_reply_ok(peer_stream, "go ahead, send me your answers");

    json_object *answers = parse_json(peer_stream);

    if (!answers) {
        send_reply_err(peer_stream, "input error");
        return -1;
    }

    if (submit_answers(id, username, answers) != 0) {
        send_reply_err(peer_stream, "submit error");
        json_object_put(answers);
        return -1;
    }

    send_reply_ok(peer_stream, "answers added");
    return 0;
}

int handle_request_put_test(const char *username, FILE *peer_stream)
{
    send_reply_ok(peer_stream, "now send me the test");
    
    json_object *test = parse_json(peer_stream);
    
    if (!test) {
        send_reply_err(peer_stream, "input error");
        return -1;
    }

    if (submit_test(username, test) != 0) {
        send_reply_err(peer_stream, "submit error");
        json_object_put(test);
        return -1;
    }

    send_reply_ok(peer_stream, "test added");
    return 0;
}

int handle_request_put_groups(FILE *peer_stream)
{
    send_reply_ok(peer_stream, "send me the groups");

    json_object *groups = parse_json(peer_stream);

    if (!groups) {
        send_reply_err(peer_stream, "input error");
        return -1;
    }

    int ret;
    if (submit_groups(groups) == 0) {
        send_reply_ok(peer_stream, "groups added");
        ret = 0;
    } else {
        send_reply_err(peer_stream, "submit error");
        ret = -1;
    }

    json_object_put(groups);

    return ret;
}

int handle_request(struct credentials *peer_creds, FILE *peer_stream)
{
    char request_line[LINE_LEN];
    if (fgets(request_line, LINE_LEN, peer_stream) == NULL)
        return -1;
    
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
        case REQUEST_USER:
        {
            const char *username = strtok_r(NULL, " \r\n", &line_ptr);
            if (!username) {
                send_reply_err(peer_stream, "invalid request");
                return -1;
            }
            return handle_request_user(username, peer_creds, peer_stream);
        }
        
        case REQUEST_GET_TESTS:
            return handle_request_get_tests(peer_creds, peer_stream);
        
        case REQUEST_GET_TEST:
        {
            const char *id_string = strtok_r(NULL, " \r\n", &line_ptr);
            uuid_t id;
            if (!id_string || uuid_parse(id_string, id) != 0) {
                send_reply_err(peer_stream, "invalid request");
                return -1;
            }
            
            return handle_request_get_test(id, peer_creds, peer_stream);
        }
        
        case REQUEST_GET_USERS:
            return handle_request_get_users(peer_creds, peer_stream);
            
        case REQUEST_GET_GROUPS:
            return handle_request_get_groups(peer_creds, peer_stream);
        
        case REQUEST_PUT_ANSWERS:
        {
            const char *id_string = strtok_r(NULL, " \r\n", &line_ptr);
            uuid_t id;
            if (!id_string || uuid_parse(id_string, id) != 0) {
                send_reply_err(peer_stream, "invalid request");
                return -1;
            }
            
            return handle_request_put_answers(id, peer_creds->username, peer_stream);
        }
        
        case REQUEST_PUT_TEST:
            return handle_request_put_test(peer_creds->username, peer_stream);
            
        case REQUEST_PUT_GROUPS:
            return handle_request_put_groups(peer_stream);
            
        case REQUEST_PUT_USER:
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
