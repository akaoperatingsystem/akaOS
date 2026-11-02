#include <syslog.h>
#include <stdio.h>
#include <stdarg.h>

void openlog(const char *ident, int option, int facility) {
    (void)ident;
    (void)option;
    (void)facility;
}

void closelog(void) {
}

int setlogmask(int mask) {
    return mask;
}

void vsyslog(int priority, const char *format, va_list ap) {
    (void)priority;
    if (format) {
        vfprintf(stderr, format, ap);
        fputc('\n', stderr);
    }
}

void syslog(int priority, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}
