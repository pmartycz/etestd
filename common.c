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

void log_msg(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_msg_real(format, ap);
    va_end(ap);
}

void log_msg_die(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_msg_real(format, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}

void log_errno(char *s)
{
    log_msg("%s: %s\n", s, strerror(errno));
}

void log_errno_die(char *s)
{
    log_msg_die("%s: %s\n", s, strerror(errno));
}
