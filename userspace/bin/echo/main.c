#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

static int write_str(int fd, const char *str) {
    size_t len = strlen(str);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, str + written, len - written);
        if (res <= 0) return 0;
        written += (size_t)res;
    }
    return 1;
}

static int emit_utf8(uint32_t cp, char *out) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Returns 1 if \c encountered (stop output entirely) */
static int print_escaped_string(const char *s) {
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '\\' && s[i+1] != '\0') {
            i++;
            char c = s[i];
            switch (c) {
                case 'a': write(STDOUT_FILENO, "\a", 1); break; // Using \a as representation, should be actual bell character 0x07
                case 'b': write(STDOUT_FILENO, "\b", 1); break;
                case 'c': return 1;
                case 'e':
                case 'E': write(STDOUT_FILENO, "\033", 1); break;
                case 'f': write(STDOUT_FILENO, "\f", 1); break;
                case 'n': write(STDOUT_FILENO, "\n", 1); break;
                case 'r': write(STDOUT_FILENO, "\r", 1); break;
                case 't': write(STDOUT_FILENO, "\t", 1); break;
                case 'v': write(STDOUT_FILENO, "\v", 1); break;
                case '\\': write(STDOUT_FILENO, "\\", 1); break;
                case '0': {
                    int val = 0;
                    int count = 0;
                    while (count < 3 && s[i+1] >= '0' && s[i+1] <= '7') {
                        val = (val << 3) | (s[++i] - '0');
                        count++;
                    }
                    char ch = (char)(val & 0xFF);
                    write(STDOUT_FILENO, &ch, 1);
                    break;
                }
                case 'x': {
                    int val = 0;
                    int count = 0;
                    while (count < 2 && hex_val(s[i+1]) >= 0) {
                        val = (val << 4) | hex_val(s[++i]);
                        count++;
                    }
                    if (count > 0) {
                        char ch = (char)(val & 0xFF);
                        write(STDOUT_FILENO, &ch, 1);
                    } else {
                        write(STDOUT_FILENO, "\\x", 2);
                    }
                    break;
                }
                case 'u': {
                    uint32_t val = 0;
                    int count = 0;
                    while (count < 4 && hex_val(s[i+1]) >= 0) {
                        val = (val << 4) | (uint32_t)hex_val(s[++i]);
                        count++;
                    }
                    if (count > 0) {
                        char ubuf[8];
                        int ulen = emit_utf8(val, ubuf);
                        write(STDOUT_FILENO, ubuf, ulen);
                    } else {
                        write(STDOUT_FILENO, "\\u", 2);
                    }
                    break;
                }
                case 'U': {
                    uint32_t val = 0;
                    int count = 0;
                    while (count < 8 && hex_val(s[i+1]) >= 0) {
                        val = (val << 4) | (uint32_t)hex_val(s[++i]);
                        count++;
                    }
                    if (count > 0) {
                        char ubuf[8];
                        int ulen = emit_utf8(val, ubuf);
                        write(STDOUT_FILENO, ubuf, ulen);
                    } else {
                        write(STDOUT_FILENO, "\\U", 2);
                    }
                    break;
                }
                default:
                    write(STDOUT_FILENO, "\\", 1);
                    write(STDOUT_FILENO, &c, 1);
                    break;
            }
        } else {
            write(STDOUT_FILENO, &s[i], 1);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    int no_newline = 0;
    int enable_escapes = 0;
    int first_arg = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-n") == 0) {
            no_newline = 1;
            first_arg++;
        } else if (strcmp(argv[i], "-e") == 0) {
            enable_escapes = 1;
            first_arg++;
        } else if (strcmp(argv[i], "-E") == 0) {
            enable_escapes = 0;
            first_arg++;
        } else if (strcmp(argv[i], "--help") == 0) {
            write_str(STDOUT_FILENO, "Usage: echo [options]... [String]...\nOptions: -n, -e, -E\n");
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            write_str(STDOUT_FILENO, "echo (akaOS coreutils) 1.0\n");
            return 0;
        } else if (strcmp(argv[i], "--") == 0) {
            first_arg = i + 1;
            break;
        } else {
            break;
        }
    }

    for (int i = first_arg; i < argc; ++i) {
        if (i > first_arg) {
            write_str(STDOUT_FILENO, " ");
        }
        if (enable_escapes) {
            if (print_escaped_string(argv[i])) break;
        } else {
            write_str(STDOUT_FILENO, argv[i]);
        }
    }
    if (!no_newline) write_str(STDOUT_FILENO, "\n");
    return 0;
}
