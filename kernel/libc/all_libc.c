#include "stdio.h"
#include "stdlib.h"
#include "ctype.h"
#include "errno.h"
#include "signal.h"
#include "../string.h"
#include "../vga.h"

extern int suppress_printf;

/* ==========================================================
 * Global libc state
 * ========================================================== */
int errno = 0;

/* ==========================================================
 * Memory Allocation (Bump Allocator for DOOM Zone memory)
 * ========================================================== */
#define HEAP_SIZE (16 * 1024 * 1024) /* 16 MB heap for DOOM */
static uint8_t doom_heap[HEAP_SIZE] __attribute__((aligned(16)));
static size_t heap_ptr = 0;

void *malloc(size_t size) {
    /* Align to 16 bytes */
    size = (size + 15) & ~(size_t)15;
    if (heap_ptr + size > HEAP_SIZE) return NULL;
    void *ptr = &doom_heap[heap_ptr];
    heap_ptr += size;
    return ptr;
}

void free(void *ptr) {
    (void)ptr; /* Bump allocator — no-op */
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (size == 0) { free(ptr); return NULL; }
    if (!ptr) return malloc(size);
    void *newptr = malloc(size);
    /* Can't track old size; caller is responsible */
    return newptr;
}

/* ==========================================================
 * Standard Library Utils
 * ========================================================== */
int abs(int j) { return j < 0 ? -j : j; }

extern void gui_force_redraw(void);
extern volatile int doom_running;
extern void *doom_exit_jmp_buf[5];

void exit(int status) {
    (void)status;
    if (doom_running) {
        doom_running = 0;
        __builtin_longjmp(doom_exit_jmp_buf, 1);
    }
    vga_print("System halted.\n");
    gui_force_redraw();
    while (1) { /* hang */ }
}

void abort(void) { exit(1); }

int atexit(void (*function)(void)) {
    (void)function;
    return 0;
}

int atoi(const char *nptr) {
    int sign = 1, val = 0;
    while (isspace((unsigned char)*nptr)) nptr++;
    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') nptr++;
    while (*nptr >= '0' && *nptr <= '9') {
        val = val * 10 + (*nptr - '0');
        nptr++;
    }
    return sign * val;
}

long atol(const char *nptr) { return (long)atoi(nptr); }

long strtol(const char *nptr, char **endptr, int base) {
    long val = 0;
    int sign = 1;
    while (isspace((unsigned char)*nptr)) nptr++;
    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') nptr++;

    if (base == 0) {
        if (*nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) { base = 16; nptr += 2; }
        else if (*nptr == '0') { base = 8; nptr++; }
        else base = 10;
    } else if (base == 16 && *nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
        nptr += 2;
    }

    while (*nptr) {
        int digit;
        if (*nptr >= '0' && *nptr <= '9') digit = *nptr - '0';
        else if (*nptr >= 'a' && *nptr <= 'f') digit = *nptr - 'a' + 10;
        else if (*nptr >= 'A' && *nptr <= 'F') digit = *nptr - 'A' + 10;
        else break;
        if (digit >= base) break;
        val = val * base + digit;
        nptr++;
    }
    if (endptr) *endptr = (char *)nptr;
    return sign * val;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtol(nptr, endptr, base);
}

char *getenv(const char *name) {
    (void)name;
    return NULL; /* No environment in bare metal */
}

static unsigned int rand_seed = 12345;
int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & 0x7FFF;
}
void srand(unsigned int seed) { rand_seed = seed; }

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    /* Simple bubble sort — DOOM uses qsort very rarely */
    char *b = (char *)base;
    char tmp[256]; /* big enough for DOOM structs */
    for (size_t i = 0; i < nmemb; i++) {
        for (size_t j = i + 1; j < nmemb; j++) {
            if (compar(b + i * size, b + j * size) > 0) {
                memcpy(tmp, b + i * size, size);
                memcpy(b + i * size, b + j * size, size);
                memcpy(b + j * size, tmp, size);
            }
        }
    }
}

/* ==========================================================
 * Ctype
 * ========================================================== */
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int ispunct(int c) { return (c > 32 && c < 127) && !isalnum(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int iscntrl(int c) { return c < 32 || c == 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* String overrides (case-insensitive) */
int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int diff = tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        if (diff != 0) return diff;
        s1++; s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    while (n-- > 0 && *s1 && *s2) {
        int diff = tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        if (diff != 0) return diff;
        s1++; s2++;
    }
    if (n < (size_t)-1) return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
    return 0;
}

/* ==========================================================
 * File I/O (Virtual WAD Loader)
 * ========================================================== */
void *doom_wad_data = NULL;
size_t doom_wad_size = 0;

static FILE virtual_wad_file = {0, 0, 0, NULL, 0};
static FILE stderr_file = {2, 0, 0, NULL, 0};
static FILE stdout_file = {1, 0, 0, NULL, 0};
static FILE stdin_file  = {0, 0, 0, NULL, 0};

FILE *stderr = &stderr_file;
FILE *stdout = &stdout_file;
FILE *stdin  = &stdin_file;

FILE *fopen(const char *filename, const char *mode) {
    (void)mode;
    
    // Debug print
    vga_print("fopen: ");
    vga_print(filename);
    vga_print("\n");
    
    if (doom_wad_data == NULL) {
        errno = ENOENT;
        return NULL;
    }
    
    // Only intercept requests for .wad files!
    size_t len = strlen(filename);
    if (len < 4) {
        errno = ENOENT;
        return NULL;
    }
    if (strcasecmp(filename + len - 4, ".wad") != 0) {
        errno = ENOENT;
        return NULL;
    }

    virtual_wad_file.data = (char *)doom_wad_data;
    virtual_wad_file.size = (int)doom_wad_size;
    virtual_wad_file.pos = 0;
    virtual_wad_file.eof_flag = 0;
    return &virtual_wad_file;
}

int fclose(FILE *stream) {
    if (stream == &virtual_wad_file) { stream->pos = 0; return 0; }
    return EOF;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    if ((size_t)stream->pos + total > (size_t)stream->size) {
        total = (size_t)stream->size - (size_t)stream->pos;
        stream->eof_flag = 1;
    }
    if (total == 0) return 0;
    memcpy(ptr, stream->data + stream->pos, total);
    stream->pos += (int)total;
    return total / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    (void)stream;
    if (suppress_printf) return nmemb;
    /* Write to console for stderr/stdout */
    const char *s = (const char *)ptr;
    size_t total = size * nmemb;
    for (size_t i = 0; i < total; i++) {
        char buf[2] = { s[i], '\0' };
        vga_print(buf);
    }
    return nmemb;
}

int fseek(FILE *stream, long offset, int whence) {
    long newpos;
    if (whence == SEEK_SET) newpos = offset;
    else if (whence == SEEK_CUR) newpos = stream->pos + offset;
    else if (whence == SEEK_END) newpos = stream->size + offset;
    else return -1;
    if (newpos < 0 || newpos > stream->size) return -1;
    stream->pos = (int)newpos;
    stream->eof_flag = 0;
    return 0;
}

long ftell(FILE *stream) { return stream->pos; }
int feof(FILE *stream) { return stream->eof_flag; }
int fflush(FILE *stream) { (void)stream; return 0; }

int fputc(int c, FILE *stream) {
    (void)stream;
    if (!suppress_printf) {
        char buf[2] = { (char)c, '\0' };
        vga_print(buf);
    }
    return c;
}

int fgetc(FILE *stream) {
    if (stream->pos >= stream->size) { stream->eof_flag = 1; return EOF; }
    return (unsigned char)stream->data[stream->pos++];
}

char *fgets(char *s, int size, FILE *stream) {
    if (size <= 0 || stream->pos >= stream->size) return NULL;
    int i;
    for (i = 0; i < size - 1 && stream->pos < stream->size; i++) {
        s[i] = stream->data[stream->pos++];
        if (s[i] == '\n') { i++; break; }
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) { (void)stream; if (!suppress_printf) vga_print(s); return 0; }
int puts(const char *s) { if (!suppress_printf) { vga_print(s); vga_print("\n"); } return 0; }
int remove(const char *pathname) { (void)pathname; return -1; }
int rename(const char *oldpath, const char *newpath) { (void)oldpath; (void)newpath; return -1; }

/* ==========================================================
 * Printf family (proper stub)
 * ========================================================== */
int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!str || size == 0) return 0;
    size_t count = 0;
    while (*format && count < size - 1) {
        if (*format == '%') {
            format++;
            
            /* Parse basic zero-padding width/precision (e.g., %03d or %.3d) */
            int pad_zero = 0;
            int width = 0;
            if (*format == '0') {
                pad_zero = 1;
                format++;
            } else if (*format == '.') {
                pad_zero = 1; /* For integers, precision padding acts like zero padding */
                format++;
            }
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }

            if (*format == 'l' || *format == 'L') format++; // ignore long flag
            
            if (*format == 's') {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && count < size - 1) str[count++] = *s++;
            } else if (*format == 'd' || *format == 'i' || *format == 'u') {
                int val = va_arg(ap, int);
                char buf[32];
                int i = 0;
                unsigned int uval;
                if (*format != 'u' && val < 0) {
                    if (count < size - 1) str[count++] = '-';
                    uval = -val;
                } else uval = (unsigned int)val;
                if (uval == 0) buf[i++] = '0';
                while (uval && i < 32) { buf[i++] = '0' + (uval % 10); uval /= 10; }
                while (pad_zero && i < width && i < 32) { buf[i++] = '0'; }
                while (i > 0 && count < size - 1) str[count++] = buf[--i];
            } else if (*format == 'x' || *format == 'X') {
                unsigned int val = va_arg(ap, unsigned int);
                char buf[32];
                int i = 0;
                if (val == 0) buf[i++] = '0';
                while (val && i < 32) {
                    int rem = val % 16;
                    buf[i++] = (rem < 10) ? '0' + rem : 'a' + rem - 10;
                    val /= 16;
                }
                while (pad_zero && i < width && i < 32) { buf[i++] = '0'; }
                while (i > 0 && count < size - 1) str[count++] = buf[--i];
            } else if (*format == 'c') {
                char c = (char)va_arg(ap, int);
                if (count < size - 1) str[count++] = c;
            } else if (*format == '%') {
                if (count < size - 1) str[count++] = '%';
            } else {
                if (count < size - 1) str[count++] = '%';
                if (count < size - 1) str[count++] = *format;
            }
        } else {
            str[count++] = *format;
        }
        format++;
    }
    str[count] = '\0';
    return count;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int res = vsnprintf(str, size, format, ap);
    va_end(ap); return res;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int res = vsnprintf(str, 1024, format, ap); /* unsafe max 1024 */
    va_end(ap); return res;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, 1024, format, ap);
}

int printf(const char *format, ...) {
    char buf[512];
    va_list ap; va_start(ap, format);
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap); 
    if (!suppress_printf) vga_print(buf); 
    return res;
}

int vprintf(const char *format, va_list ap) {
    char buf[512];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    if (!suppress_printf) vga_print(buf); 
    return res;
}

int fprintf(FILE *stream, const char *format, ...) {
    char buf[512];
    va_list ap; va_start(ap, format);
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap); (void)stream; 
    if (!suppress_printf) vga_print(buf); 
    return res;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[512];
    int res = vsnprintf(buf, sizeof(buf), format, ap);
    (void)stream; 
    if (!suppress_printf) vga_print(buf); 
    return res;
}

int sscanf(const char *str, const char *format, ...) {
    /* DOOM hardly uses sscanf, so a safe stub is to return 0 */
    (void)str; (void)format; return 0;
}

/* ==========================================================
 * Signal (stub)
 * ========================================================== */
sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum; (void)handler;
    return SIG_DFL;
}

/* ==========================================================
 * Unistd / stat stubs
 * ========================================================== */
int access(const char *pathname, int mode) { (void)pathname; (void)mode; return -1; }
unsigned int sleep(unsigned int seconds) { (void)seconds; return 0; }
int usleep(unsigned long usec) { (void)usec; return 0; }
char *getcwd(char *buf, size_t size) { if (buf && size > 0) { buf[0] = '/'; buf[1] = '\0'; } return buf; }
int chdir(const char *path) { (void)path; return 0; }
int isatty(int fd) { (void)fd; return 1; }

struct stat;
int stat(const char *path, struct stat *buf) { (void)path; (void)buf; return -1; }
int mkdir(const char *path, int mode) { (void)path; (void)mode; return -1; }

/* ==========================================================
 * Extra stubs for DOOM
 * ========================================================== */
int putchar(int c) {
    if (!suppress_printf) {
        char buf[2] = { (char)c, '\0' };
        vga_print(buf);
    }
    return c;
}

int system(const char *command) { (void)command; return -1; }

#if defined(ARCH_X86_64) || defined(ARCH_X86_32)
__attribute__((target("sse2")))
#endif
double atof(const char *nptr) {
    (void)nptr;
    return 0.0;
}

#if defined(ARCH_X86_64) || defined(ARCH_X86_32)
__attribute__((target("sse2")))
#endif
double fabs(double x) {
    return x < 0.0 ? -x : x;
}
