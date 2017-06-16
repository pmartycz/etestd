/**
 * @file
 * @author Piotr Martycz <pmartycz@gmail.com>
 *
 * @section DESCRIPTION
 * Module for code shared between other modules.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "common.h"

static void log_msg_real(char *format, va_list ap)
{
    vfprintf(stderr, format, ap);
}

/**
 * Log a message.
 *
 * Takes a printf-style format string with zero or more arguments.
 * 
 * @param format format string
 */
void log_msg(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_msg_real(format, ap);
    va_end(ap);
}

/**
 * Log a message and terminate server.
 *
 * Takes a printf-style format string with zero or more arguments.
 *
 * @param format format string
 */
void log_msg_die(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_msg_real(format, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}

/**
 * Log description of current errno value.
 *
 * @param s Informational string for user.
 */
void log_errno(char *s)
{
    log_msg("%s: %s\n", s, strerror(errno));
}

/**
 * Log description of current errno value
 * and terminate server.
 *
 * @param s Informational string for user.
 */
void log_errno_die(char *s)
{
    log_msg_die("%s: %s\n", s, strerror(errno));
}

/**
 * Check strings for equality.
 *
 * @param a first string
 * @param b second string
 *
 * @return Zero if strings are equal.
 */
int streq(const char *a, const char *b)
{
    return !strcmp(a, b);
}
