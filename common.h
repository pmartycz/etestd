#include <assert.h>

void log_msg(char *format, ...) __attribute__ ((format (printf, 1, 2)));

void log_msg_die(char *format, ...) __attribute__ ((format (printf, 1, 2))); 

void log_errno(char *s);

void log_errno_die(char *s);

int streq(const char *a, const char *b);
