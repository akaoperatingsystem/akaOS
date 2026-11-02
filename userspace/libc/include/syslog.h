#ifndef AKAOS_LIBC_SYSLOG_H
#define AKAOS_LIBC_SYSLOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

#define LOG_PID    0x01
#define LOG_CONS   0x02
#define LOG_ODELAY 0x04
#define LOG_NDELAY 0x08
#define LOG_NOWAIT 0x10
#define LOG_PERROR 0x20

#define LOG_KERN     (0<<3)
#define LOG_USER     (1<<3)
#define LOG_MAIL     (2<<3)
#define LOG_DAEMON   (3<<3)
#define LOG_AUTH     (4<<3)
#define LOG_SYSLOG   (5<<3)
#define LOG_LPR      (6<<3)
#define LOG_NEWS     (7<<3)
#define LOG_UUCP     (8<<3)
#define LOG_CRON     (9<<3)
#define LOG_AUTHPRIV (10<<3)
#define LOG_LOCAL0   (16<<3)

void openlog(const char *ident, int option, int facility);
void syslog(int priority, const char *format, ...);
void closelog(void);
void vsyslog(int priority, const char *format, va_list ap);
int setlogmask(int mask);

#ifdef __cplusplus
}
#endif

#endif /* AKAOS_LIBC_SYSLOG_H */
