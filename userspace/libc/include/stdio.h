#ifndef _STDIO_H
#define _STDIO_H

#include <features.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EOF (-1)
#define BUFSIZ 4096
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct __akaos_FILE FILE;
typedef off_t fpos_t;
extern FILE __akaos_stdin;
extern FILE __akaos_stdout;
extern FILE __akaos_stderr;
#define stdin  (&__akaos_stdin)
#define stdout (&__akaos_stdout)
#define stderr (&__akaos_stderr)

int printf(const char *format, ...);
int vprintf(const char *format, va_list ap);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int vsprintf(char *str, const char *format, va_list ap);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);
int fdprintf(int fd, const char *format, ...);
int vfdprintf(int fd, const char *format, va_list ap);
FILE *fopen(const char *path, const char *mode);
FILE *tmpfile(void);
FILE *open_memstream(char **buffer, size_t *length);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int fclose(FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fseeko(FILE *stream, off_t offset, int whence);
off_t ftello(FILE *stream);
void rewind(FILE *stream);
int fgetpos(FILE *stream, fpos_t *position);
int fsetpos(FILE *stream, const fpos_t *position);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
void setbuf(FILE *stream, char *buffer);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fileno(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
size_t fread_unlocked(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite_unlocked(const void *buffer, size_t size, size_t count, FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int fgetc_unlocked(FILE *stream);
int getc_unlocked(FILE *stream);
int getchar(void);
int getchar_unlocked(void);
void flockfile(FILE *stream);
int ftrylockfile(FILE *stream);
void funlockfile(FILE *stream);
int ungetc(int character, FILE *stream);
int fputc(int character, FILE *stream);
int putc(int character, FILE *stream);
int fputc_unlocked(int character, FILE *stream);
int putc_unlocked(int character, FILE *stream);
int putchar(int character);
int putchar_unlocked(int character);
char *fgets(char *buffer, int capacity, FILE *stream);
ssize_t getdelim(char **lineptr, size_t *capacity, int delimiter, FILE *stream);
ssize_t getline(char **lineptr, size_t *capacity, FILE *stream);
int fputs(const char *text, FILE *stream);
int puts(const char *text);
int vfscanf(FILE *stream, const char *format, va_list ap);
int fscanf(FILE *stream, const char *format, ...);
int scanf(const char *format, ...);
int vasprintf(char **strp, const char *format, va_list ap);
int asprintf(char **strp, const char *format, ...);
int vsscanf(const char *input, const char *format, va_list ap);
int sscanf(const char *input, const char *format, ...);

void perror(const char *s);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
int renameat(int old_directory, const char *oldpath, int new_directory,
             const char *newpath);

#ifdef __cplusplus
}
#else
#ifndef AKAOS_LIBC_STDIO_IMPLEMENTATION
/* Keep akaOS's historical descriptor form working in C programs while the
 * FILE* form remains the standard interface used by C++ and POSIX libraries. */
#define fprintf(stream, ...) \
    _Generic((stream), FILE *: fprintf, const FILE *: fprintf, \
             void *: fprintf, const void *: fprintf, \
             default: fdprintf)((stream), __VA_ARGS__)
#define vfprintf(stream, format, ap) \
    _Generic((stream), FILE *: vfprintf, const FILE *: vfprintf, \
             void *: vfprintf, const void *: vfprintf, \
             default: vfdprintf)((stream), (format), (ap))
#endif
#endif

#endif
