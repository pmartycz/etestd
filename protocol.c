#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "protocol.h"

static int request_required_auth_levels[] = {
    /* REQUEST_INVALID */       0,
    /* REQUEST_USER */          AUTH_LEVEL_UNAUTHORIZED,

    /* Student can only fetch tests available to him,
     * Examinator can only fetch tests he owns */
      
    /* REQUEST_GET_TEST */      AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINATOR,
    /* REQUEST_GET_TESTS */     AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINATOR,

    /* Student can only fetch users in examinators group */
    
    /* REQUEST_GET_USERS */     AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINATOR | AUTH_LEVEL_ADMINISTRATOR,
    /* REQUEST_GET_GROUPS */    AUTH_LEVEL_EXAMINATOR | AUTH_LEVEL_ADMINISTRATOR,

    /* Student submits answers for tests available to him,
     * subject to time restrictions */
     
    /* REQUEST_PUT_ANSWERS */   AUTH_LEVEL_STUDENT,
    
    /* REQUEST_PUT_TEST */      AUTH_LEVEL_EXAMINATOR,
    /* REQUEST_PUT_USER */      AUTH_LEVEL_ADMINISTRATOR,
    /* REQUEST_PUT_GROUP */     AUTH_LEVEL_ADMINISTRATOR,
    
    /* Examinator can only delete test he owns, administrator can delete any test */
    
    /* REQUEST_DELETE_TEST */   AUTH_LEVEL_EXAMINATOR,
    /* REQUEST_DELETE_USER */   AUTH_LEVEL_ADMINISTRATOR,
    /* REQUEST_DELETE_GROUP */  AUTH_LEVEL_ADMINISTRATOR,
    /* REQUEST_BYE */           AUTH_LEVEL_UNAUTHORIZED | AUTH_LEVEL_STUDENT | AUTH_LEVEL_EXAMINATOR | AUTH_LEVEL_ADMINISTRATOR
};

int has_required_auth_level(int request_type, int client_auth_level)
{
    if (request_type >= sizeof(request_required_auth_levels) / sizeof(int) || request_type < 0) {
        log_msg("Invalid request type %i\n", request_type);
        return 0;
    }
    
    return request_required_auth_levels[request_type] & client_auth_level;
}

static int send_reply(FILE *stream, int reply_type, const char *format, va_list ap)
{
    const char *sig;
    
    if (reply_type == REPLY_OK)
        sig = "+OK ";
    else if (reply_type == REPLY_ERR)
        sig = "+ERR ";
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

struct map {
    const char *name;
    int value;
};

static const struct request_map {
    const char *method;
    const struct map *mappings;
} request_map_table[] = {
    {
        "USER",
        (struct map []) {
            {   "",         REQUEST_USER            },
            {   NULL,       0                       }
        }
    },
    {
        "GET",
        (struct map []) {
            {   "TEST",     REQUEST_GET_TEST        },
            {   "TESTS",    REQUEST_GET_TESTS       },
            {   "USERS",    REQUEST_GET_USERS       },
            {   "GROUPS",   REQUEST_GET_GROUPS      },
            {   NULL,       0 }
        }
    },
    {
        "PUT",
        (struct map []) {
            {   "ANSWERS",  REQUEST_PUT_ANSWERS     },
            {   "TEST",     REQUEST_PUT_TEST        },
            {   "USER",     REQUEST_PUT_USER        },
            {   "GROUP",    REQUEST_PUT_GROUP       },
            {   NULL,       0                       }
        }
    },
    {
        "DELETE",
        (struct map []) {
            {   "TEST",     REQUEST_DELETE_TEST     },
            {   "USER",     REQUEST_DELETE_USER     },
            {   "GROUP",    REQUEST_DELETE_GROUP    },
            {   NULL,       0                       }
        }
    },
    {
        "BYE",
        (struct map []) {
            {   "",         REQUEST_BYE             },
            {   NULL,       0                       }
        }
    },
    {
        NULL,
        NULL
    }
};

/* @warning Clobbers request_line */
int parse_request(char *request_line)
{
    char *first_token = strtok(request_line, " \r\n");
    if (!first_token)
        return REQUEST_INVALID;     /* empty request, etc */
        
    char *second_token = strtok(NULL, " \r\n");
    if (!second_token)
        second_token = "";          /* no arg supplied */

    for (const struct request_map *t = request_map_table; t->method != NULL; t++)
        if (strcasecmp(first_token, t->method) == 0)
            for (const struct map *mapping = t->mappings; mapping->name; mapping++)
                if (strcasecmp(second_token, mapping->name) == 0)
                    return mapping->value;
                    
    return REQUEST_INVALID;
}
