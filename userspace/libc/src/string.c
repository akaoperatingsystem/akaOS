#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <fnmatch.h>

int strerror_r(int errnum, char *buffer, size_t capacity) {
    if (!buffer || capacity == 0U) return EINVAL;
    const char *message = strerror(errnum);
    size_t length = strlen(message);
    if (length >= capacity) {
        if (capacity > 1U) {
            memcpy(buffer, message, capacity - 1U);
            buffer[capacity - 1U] = '\0';
        } else {
            buffer[0] = '\0';
        }
        return ERANGE;
    }
    memcpy(buffer, message, length + 1U);
    return 0;
}

static unsigned char fnmatch_fold(unsigned char value, int flags) {
    if ((flags & FNM_CASEFOLD) && value >= 'A' && value <= 'Z')
        return (unsigned char)(value + ('a' - 'A'));
    return value;
}

static int fnmatch_component_start(const char *string, size_t index,
                                   int flags) {
    return index == 0U ||
           ((flags & FNM_PATHNAME) != 0 && string[index - 1U] == '/');
}

static int fnmatch_bracket(const char **pattern_cursor, unsigned char value,
                           int flags, int *valid) {
    const char *cursor = *pattern_cursor + 1;
    int negate = 0;
    int matched = 0;
    int has_element = 0;
    if (*cursor == '!' || *cursor == '^') {
        negate = 1;
        ++cursor;
    }
    if (*cursor == ']') {
        has_element = 1;
        matched = value == ']';
        ++cursor;
    }
    while (*cursor && *cursor != ']') {
        unsigned char first = (unsigned char)*cursor++;
        if (first == '\\' && !(flags & FNM_NOESCAPE) && *cursor)
            first = (unsigned char)*cursor++;
        unsigned char folded_first = fnmatch_fold(first, flags);
        has_element = 1;
        if (*cursor == '-' && cursor[1] && cursor[1] != ']') {
            ++cursor;
            unsigned char last = (unsigned char)*cursor++;
            if (last == '\\' && !(flags & FNM_NOESCAPE) && *cursor)
                last = (unsigned char)*cursor++;
            unsigned char folded_last = fnmatch_fold(last, flags);
            unsigned char folded_value = fnmatch_fold(value, flags);
            if (folded_first <= folded_last && folded_value >= folded_first &&
                folded_value <= folded_last)
                matched = 1;
        } else if (folded_first == fnmatch_fold(value, flags)) {
            matched = 1;
        }
    }
    if (*cursor != ']' || !has_element) {
        *valid = 0;
        return 0;
    }
    *pattern_cursor = cursor + 1;
    *valid = 1;
    return negate ? !matched : matched;
}

int fnmatch(const char *pattern, const char *string, int flags) {
    if (!pattern || !string) return FNM_NOMATCH;
    size_t pattern_length = strlen(pattern);
    size_t string_length = strlen(string);
    if (string_length == SIZE_MAX) return FNM_NOMATCH;
    unsigned char *previous = malloc(string_length + 1U);
    unsigned char *current = malloc(string_length + 1U);
    if (!previous || !current) {
        free(previous);
        free(current);
        errno = ENOMEM;
        return FNM_NOMATCH;
    }
    memset(previous, 0, string_length + 1U);
    previous[0] = 1U;

    const char *cursor = pattern;
    while ((size_t)(cursor - pattern) < pattern_length) {
        unsigned char token = (unsigned char)*cursor++;
        if (token == '*' ) {
            while (*cursor == '*') ++cursor;
            current[0] = previous[0];
            for (size_t index = 1U; index <= string_length; ++index) {
                unsigned char character = (unsigned char)string[index - 1U];
                int may_consume =
                    !((flags & FNM_PATHNAME) && character == '/') &&
                    !((flags & FNM_PERIOD) && character == '.' &&
                      fnmatch_component_start(string, index - 1U, flags));
                current[index] = (unsigned char)(previous[index] ||
                    (current[index - 1U] && may_consume));
            }
        } else {
            int bracket = 0;
            int bracket_valid = 0;
            const char *after_token = cursor;
            int token_is_question = token == '?';
            if (token == '[') {
                after_token = cursor - 1U;
                bracket = fnmatch_bracket(&after_token, 0U, flags,
                                         &bracket_valid);
            }
            (void)bracket;
            if (token == '[' && !bracket_valid) {
                token = '[';
                after_token = cursor;
            }
            if (token == '\\' && !(flags & FNM_NOESCAPE) && *cursor) {
                token = (unsigned char)*cursor++;
                token_is_question = 0;
            }
            current[0] = 0U;
            for (size_t index = 1U; index <= string_length; ++index) {
                unsigned char character = (unsigned char)string[index - 1U];
                int token_matches;
                if (bracket_valid) {
                    const char *bracket_cursor = cursor - 1U;
                    int ignored_valid = 0;
                    token_matches = fnmatch_bracket(
                        &bracket_cursor, character, flags, &ignored_valid);
                } else {
                    token_matches = fnmatch_fold(token, flags) ==
                                    fnmatch_fold(character, flags);
                }
                int wildcard_period =
                    (bracket_valid || token_is_question) &&
                    character == '.' && (flags & FNM_PERIOD) &&
                    fnmatch_component_start(string, index - 1U, flags);
                int wildcard_slash = bracket_valid || token_is_question;
                if (previous[index - 1U] && token_matches &&
                    !wildcard_period &&
                    !((flags & FNM_PATHNAME) && wildcard_slash &&
                      character == '/'))
                    current[index] = 1U;
                else
                    current[index] = 0U;
            }
            if (bracket_valid) cursor = after_token;
        }
        unsigned char *swap = previous;
        previous = current;
        current = swap;
    }

    int matched = previous[string_length] != 0U;
    if (!matched && (flags & FNM_LEADING_DIR)) {
        for (size_t index = 0U; index < string_length; ++index) {
            if (string[index] == '/' && previous[index]) {
                matched = 1;
                break;
            }
        }
    }
    free(previous);
    free(current);
    return matched ? 0 : FNM_NOMATCH;
}

static int ascii_lower(int character) {
    if (character >= 'A' && character <= 'Z') return character + ('a' - 'A');
    return character;
}

int isalnum(int character) {
    return (character >= '0' && character <= '9') ||
           (ascii_lower(character) >= 'a' && ascii_lower(character) <= 'z');
}
int isalpha(int character) {
    character = ascii_lower(character);
    return character >= 'a' && character <= 'z';
}
int isblank(int character) { return character == ' ' || character == '\t'; }
int iscntrl(int character) {
    return (character >= 0 && character < 0x20) || character == 0x7f;
}
int isdigit(int character) { return character >= '0' && character <= '9'; }
int isgraph(int character) { return character > 0x20 && character < 0x7f; }
int islower(int character) { return character >= 'a' && character <= 'z'; }
int isprint(int character) { return character >= 0x20 && character < 0x7f; }
int ispunct(int character) {
    return isgraph(character) && !isalnum(character);
}
int isspace(int character) {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\v' || character == '\f';
}
int isupper(int character) { return character >= 'A' && character <= 'Z'; }
int isxdigit(int character) {
    return isdigit(character) ||
           (ascii_lower(character) >= 'a' && ascii_lower(character) <= 'f');
}
int tolower(int character) { return ascii_lower(character); }
int isascii(int character) { return (character & ~0x7f) == 0; }
int toascii(int character) { return character & 0x7f; }
int toupper(int character) {
    if (character >= 'a' && character <= 'z') return character - ('a' - 'A');
    return character;
}

/* akaOS currently ships the POSIX C and C.UTF-8 locales.  Both use the same
 * ASCII byte collation and case rules, so the _l entry points share the
 * locale-independent implementation until additional locale data is added. */
int isalnum_l(int c, locale_t locale) { (void)locale; return isalnum(c); }
int isalpha_l(int c, locale_t locale) { (void)locale; return isalpha(c); }
int isblank_l(int c, locale_t locale) { (void)locale; return isblank(c); }
int iscntrl_l(int c, locale_t locale) { (void)locale; return iscntrl(c); }
int isdigit_l(int c, locale_t locale) { (void)locale; return isdigit(c); }
int isgraph_l(int c, locale_t locale) { (void)locale; return isgraph(c); }
int islower_l(int c, locale_t locale) { (void)locale; return islower(c); }
int isprint_l(int c, locale_t locale) { (void)locale; return isprint(c); }
int ispunct_l(int c, locale_t locale) { (void)locale; return ispunct(c); }
int isspace_l(int c, locale_t locale) { (void)locale; return isspace(c); }
int isupper_l(int c, locale_t locale) { (void)locale; return isupper(c); }
int isxdigit_l(int c, locale_t locale) { (void)locale; return isxdigit(c); }
int tolower_l(int c, locale_t locale) { (void)locale; return tolower(c); }
int toupper_l(int c, locale_t locale) { (void)locale; return toupper(c); }

int strcoll(const char *left, const char *right) {
    return strcmp(left, right);
}

size_t strxfrm(char *destination, const char *source, size_t count) {
    size_t length = strlen(source);
    if (destination && count != 0U) {
        size_t copied = length < count - 1U ? length : count - 1U;
        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

int strcoll_l(const char *left, const char *right, locale_t locale) {
    (void)locale;
    return strcoll(left, right);
}

size_t strxfrm_l(char *destination, const char *source, size_t count,
                 locale_t locale) {
    (void)locale;
    return strxfrm(destination, source, count);
}

/* Strict UTF-8 codec used by locale-aware desktop source ports.  Invalid
 * overlong encodings, surrogate code points, and values above U+10FFFF are
 * rejected instead of being silently normalized. */
static int utf8_decode(const unsigned char *bytes, size_t length,
                       uint32_t *value, size_t *consumed) {
    if (!bytes || !value || !consumed || length == 0) return 1;
    unsigned char first = bytes[0];
    uint32_t codepoint;
    size_t width;
    if (first < 0x80U) {
        codepoint = first;
        width = 1;
    } else if (first >= 0xc2U && first <= 0xdfU) {
        codepoint = first & 0x1fU;
        width = 2;
    } else if (first >= 0xe0U && first <= 0xefU) {
        codepoint = first & 0x0fU;
        width = 3;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        codepoint = first & 0x07U;
        width = 4;
    } else {
        return -1;
    }
    if (length < width) return 1;
    for (size_t index = 1; index < width; ++index) {
        if ((bytes[index] & 0xc0U) != 0x80U) return -1;
        codepoint = (codepoint << 6) | (bytes[index] & 0x3fU);
    }
    if ((width == 3 && codepoint < 0x800U) ||
        (width == 4 && codepoint < 0x10000U) ||
        codepoint > 0x10ffffU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU)) return -1;
    *value = codepoint;
    *consumed = width;
    return 0;
}

size_t mbrtowc(wchar_t *wide_character, const char *multibyte,
               size_t length, mbstate_t *state) {
    if (state) state->value = 0;
    if (!multibyte) return 0;
    if (__akaos_mb_cur_max() == 1U &&
        (unsigned char)multibyte[0] > 0x7fU) return (size_t)-1;
    uint32_t codepoint = 0;
    size_t consumed = 0;
    int result = utf8_decode((const unsigned char *)multibyte, length,
                             &codepoint, &consumed);
    if (result > 0) return (size_t)-2;
    if (result < 0) return (size_t)-1;
    if (wide_character) *wide_character = (wchar_t)codepoint;
    return codepoint == 0 ? 0 : consumed;
}

size_t wcrtomb(char *multibyte, wchar_t wide_character, mbstate_t *state) {
    if (state) state->value = 0;
    uint32_t codepoint = (uint32_t)wide_character;
    if (!multibyte) return 1;
    if (__akaos_mb_cur_max() == 1U && codepoint > 0x7fU) return (size_t)-1;
    if (codepoint > 0x10ffffU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU)) return (size_t)-1;
    if (codepoint < 0x80U) {
        multibyte[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800U) {
        multibyte[0] = (char)(0xc0U | (codepoint >> 6));
        multibyte[1] = (char)(0x80U | (codepoint & 0x3fU));
        return 2;
    }
    if (codepoint < 0x10000U) {
        multibyte[0] = (char)(0xe0U | (codepoint >> 12));
        multibyte[1] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
        multibyte[2] = (char)(0x80U | (codepoint & 0x3fU));
        return 3;
    }
    multibyte[0] = (char)(0xf0U | (codepoint >> 18));
    multibyte[1] = (char)(0x80U | ((codepoint >> 12) & 0x3fU));
    multibyte[2] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
    multibyte[3] = (char)(0x80U | (codepoint & 0x3fU));
    return 4;
}

int mbtowc(wchar_t *wide_character, const char *multibyte, size_t length) {
    if (!multibyte) return 0;
    size_t converted = mbrtowc(wide_character, multibyte, length, NULL);
    if (converted == (size_t)-1 || converted == (size_t)-2) return -1;
    return converted == 0 ? 0 : (int)converted;
}

size_t mbrlen(const char *multibyte, size_t length, mbstate_t *state) {
    return mbrtowc(NULL, multibyte, length, state);
}

/* The legacy X/Open spelling of the same operation.  Upstream X11 sources
 * still call it directly. */
int mblen(const char *multibyte, size_t length) {
    size_t result = mbrlen(multibyte, length, NULL);
    if (result == (size_t)-1 || result == (size_t)-2) return -1;
    return (int)result;
}

wint_t btowc(int character) {
    if (character < 0 || character > 0x7f) return WEOF;
    return (wint_t)character;
}

int wctob(wint_t character) {
    return character <= 0x7fU ? (int)character : -1;
}

int wctomb(char *multibyte, wchar_t character) {
    if (!multibyte) return 0;
    size_t result = wcrtomb(multibyte, character, NULL);
    return result == (size_t)-1 ? -1 : (int)result;
}

size_t mbsnrtowcs(wchar_t *wide, const char **multibyte, size_t bytes,
                  size_t count, mbstate_t *state) {
    if (!multibyte || !*multibyte) return (size_t)-1;
    const char *input = *multibyte;
    size_t consumed_bytes = 0;
    size_t converted = 0;
    while (consumed_bytes < bytes && (!wide || converted < count)) {
        wchar_t value = 0;
        size_t amount = mbrtowc(&value, input + consumed_bytes,
                                bytes - consumed_bytes, state);
        if (amount == (size_t)-1) return amount;
        if (amount == (size_t)-2) break;
        if (amount == 0) {
            if (wide) {
                wide[converted] = 0;
                *multibyte = NULL;
            }
            return converted;
        }
        if (wide) wide[converted] = value;
        consumed_bytes += amount;
        ++converted;
    }
    if (wide) *multibyte = input + consumed_bytes;
    return converted;
}

void bzero(void *s, size_t n) {
    if (s && n) memset(s, 0, n);
}

void bcopy(const void *src, void *dest, size_t n) {
    if (src && dest && n) memmove(dest, src, n);
}

int bcmp(const void *s1, const void *s2, size_t n) {
    if (!s1 || !s2 || !n) return 0;
    return memcmp(s1, s2, n);
}

size_t wcsnrtombs(char *multibyte, const wchar_t **wide, size_t count,
                  size_t bytes, mbstate_t *state) {
    if (!wide || !*wide) return (size_t)-1;
    const wchar_t *input = *wide;
    size_t converted = 0;
    size_t output = 0;
    while (converted < count) {
        if (input[converted] == 0) {
            if (multibyte && output < bytes) {
                multibyte[output] = '\0';
                *wide = NULL;
            } else if (!multibyte) {
                *wide = input;
            }
            return output;
        }
        char encoded[4];
        size_t amount = wcrtomb(encoded, input[converted], state);
        if (amount == (size_t)-1) return amount;
        if (multibyte && amount > bytes - output) break;
        if (multibyte) memcpy(multibyte + output, encoded, amount);
        output += amount;
        ++converted;
    }
    if (multibyte) *wide = input + converted;
    else *wide = input;
    return output;
}

size_t mbsrtowcs(wchar_t *wide, const char **multibyte, size_t count,
                 mbstate_t *state) {
    if (!multibyte || !*multibyte) return (size_t)-1;
    return mbsnrtowcs(wide, multibyte, strlen(*multibyte) + 1U,
                      count, state);
}

size_t wcsrtombs(char *multibyte, const wchar_t **wide, size_t bytes,
                 mbstate_t *state) {
    if (!wide || !*wide) return (size_t)-1;
    return wcsnrtombs(multibyte, wide, wcslen(*wide) + 1U, bytes, state);
}

size_t mbstowcs(wchar_t *wide, const char *multibyte, size_t count) {
    if (!multibyte) return (size_t)-1;
    size_t input = 0;
    size_t output = 0;
    while (multibyte[input] != '\0') {
        wchar_t value = 0;
        size_t consumed = mbrtowc(&value, multibyte + input,
                                  strlen(multibyte + input), NULL);
        if (consumed == (size_t)-1 || consumed == (size_t)-2) return (size_t)-1;
        if (wide) {
            if (output >= count) break;
            wide[output] = value;
        }
        ++output;
        input += consumed;
    }
    if (wide && output < count) wide[output] = 0;
    return output;
}

size_t wcstombs(char *multibyte, const wchar_t *wide, size_t count) {
    if (!wide) return (size_t)-1;
    size_t input = 0;
    size_t output = 0;
    while (wide[input] != 0) {
        char encoded[4];
        size_t bytes = wcrtomb(encoded, wide[input], NULL);
        if (bytes == (size_t)-1) return (size_t)-1;
        if (multibyte) {
            if (output + bytes > count) break;
            memcpy(multibyte + output, encoded, bytes);
        }
        output += bytes;
        ++input;
    }
    if (multibyte && output < count) multibyte[output] = '\0';
    return output;
}

size_t wcslen(const wchar_t *wide) {
    size_t length = 0;
    if (!wide) return 0;
    while (wide[length]) ++length;
    return length;
}

int wcscmp(const wchar_t *left, const wchar_t *right) {
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left < *right ? -1 : (*left > *right ? 1 : 0);
}

char *strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    size_t needle_len = strlen(needle);
    for (; *haystack; ++haystack) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
    }
    return NULL;
}

int wcsncmp(const wchar_t *left, const wchar_t *right, size_t count) {
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (count && *left && *left == *right) {
        ++left;
        ++right;
        --count;
    }
    return count == 0U ? 0 : (*left < *right ? -1 : (*left > *right ? 1 : 0));
}

wchar_t *wcschr(const wchar_t *wide, wchar_t character) {
    if (!wide) return NULL;
    do {
        if (*wide == character) return (wchar_t *)wide;
    } while (*wide++ != L'\0');
    return NULL;
}

wchar_t *wcsrchr(const wchar_t *wide, wchar_t character) {
    if (!wide) return NULL;
    const wchar_t *match = NULL;
    do {
        if (*wide == character) match = wide;
    } while (*wide++ != L'\0');
    return (wchar_t *)match;
}

wchar_t *wcsstr(const wchar_t *wide, const wchar_t *needle) {
    if (!wide || !needle) return NULL;
    if (*needle == L'\0') return (wchar_t *)wide;
    for (; *wide; ++wide) {
        const wchar_t *candidate = wide;
        const wchar_t *suffix = needle;
        while (*candidate && *suffix && *candidate == *suffix) {
            ++candidate;
            ++suffix;
        }
        if (*suffix == L'\0') return (wchar_t *)wide;
    }
    return NULL;
}

wchar_t *wcscpy(wchar_t *destination, const wchar_t *source) {
    if (!destination || !source) return destination;
    wchar_t *result = destination;
    do {
        *destination++ = *source;
    } while (*source++ != L'\0');
    return result;
}

wchar_t *wcsncpy(wchar_t *destination, const wchar_t *source, size_t count) {
    if (!destination || !source) return destination;
    wchar_t *result = destination;
    size_t copied = 0U;
    while (copied < count && source[copied] != L'\0') {
        destination[copied] = source[copied];
        ++copied;
    }
    while (copied < count) destination[copied++] = L'\0';
    return result;
}

wchar_t *wcscat(wchar_t *destination, const wchar_t *source) {
    if (!destination || !source) return destination;
    wcscpy(destination + wcslen(destination), source);
    return destination;
}

wchar_t *wcsncat(wchar_t *destination, const wchar_t *source, size_t count) {
    if (!destination || !source) return destination;
    wchar_t *tail = destination + wcslen(destination);
    size_t copied = 0U;
    while (copied < count && source[copied] != L'\0') {
        tail[copied] = source[copied];
        ++copied;
    }
    tail[copied] = L'\0';
    return destination;
}

static int wide_contains(const wchar_t *set, wchar_t character) {
    if (!set) return 0;
    while (*set) {
        if (*set++ == character) return 1;
    }
    return 0;
}

wchar_t *wcspbrk(const wchar_t *wide, const wchar_t *accept) {
    if (!wide || !accept) return NULL;
    for (; *wide; ++wide) {
        if (wide_contains(accept, *wide)) return (wchar_t *)wide;
    }
    return NULL;
}

size_t wcscspn(const wchar_t *wide, const wchar_t *reject) {
    size_t count = 0U;
    if (!wide || !reject) return 0U;
    while (wide[count] && !wide_contains(reject, wide[count])) ++count;
    return count;
}

size_t wcsspn(const wchar_t *wide, const wchar_t *accept) {
    size_t count = 0U;
    if (!wide || !accept) return 0U;
    while (wide[count] && wide_contains(accept, wide[count])) ++count;
    return count;
}

wchar_t *wmemchr(const wchar_t *wide, wchar_t character, size_t count) {
    if (!wide) return NULL;
    for (size_t index = 0U; index < count; ++index) {
        if (wide[index] == character) return (wchar_t *)(wide + index);
    }
    return NULL;
}

wchar_t *wmemcpy(wchar_t *destination, const wchar_t *source, size_t count) {
    if (!destination || !source) return destination;
    for (size_t index = 0U; index < count; ++index)
        destination[index] = source[index];
    return destination;
}

wchar_t *wmemmove(wchar_t *destination, const wchar_t *source, size_t count) {
    if (!destination || !source || destination == source) return destination;
    uintptr_t destination_address = (uintptr_t)destination;
    uintptr_t source_address = (uintptr_t)source;
    if (destination_address < source_address) {
        for (size_t index = 0U; index < count; ++index)
            destination[index] = source[index];
    } else {
        for (size_t index = count; index > 0U; --index)
            destination[index - 1U] = source[index - 1U];
    }
    return destination;
}

wchar_t *wmemset(wchar_t *wide, wchar_t character, size_t count) {
    if (!wide) return NULL;
    for (size_t index = 0U; index < count; ++index) wide[index] = character;
    return wide;
}

int wmemcmp(const wchar_t *left, const wchar_t *right, size_t count) {
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    for (size_t index = 0U; index < count; ++index) {
        if (left[index] != right[index])
            return left[index] < right[index] ? -1 : 1;
    }
    return 0;
}

int iswalpha(wint_t character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z');
}
int iswalnum(wint_t character) {
    return iswalpha(character) || (character >= '0' && character <= '9');
}
int iswdigit(wint_t character) {
    return character >= '0' && character <= '9';
}
int iswspace(wint_t character) {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\v' || character == '\f' ||
           character == 0x00a0U;
}
int towlower(wint_t character) {
    return character >= 'A' && character <= 'Z' ?
           (int)(character + ('a' - 'A')) : (int)character;
}
int towupper(wint_t character) {
    return character >= 'a' && character <= 'z' ?
           (int)(character - ('a' - 'A')) : (int)character;
}

enum {
    AKAOS_WCTYPE_ALNUM = 1,
    AKAOS_WCTYPE_ALPHA,
    AKAOS_WCTYPE_BLANK,
    AKAOS_WCTYPE_CNTRL,
    AKAOS_WCTYPE_DIGIT,
    AKAOS_WCTYPE_GRAPH,
    AKAOS_WCTYPE_LOWER,
    AKAOS_WCTYPE_PRINT,
    AKAOS_WCTYPE_PUNCT,
    AKAOS_WCTYPE_SPACE,
    AKAOS_WCTYPE_UPPER,
    AKAOS_WCTYPE_XDIGIT
};

wctype_t wctype(const char *property) {
    if (!property) return 0;
    if (strcmp(property, "alnum") == 0) return AKAOS_WCTYPE_ALNUM;
    if (strcmp(property, "alpha") == 0) return AKAOS_WCTYPE_ALPHA;
    if (strcmp(property, "blank") == 0) return AKAOS_WCTYPE_BLANK;
    if (strcmp(property, "cntrl") == 0) return AKAOS_WCTYPE_CNTRL;
    if (strcmp(property, "digit") == 0) return AKAOS_WCTYPE_DIGIT;
    if (strcmp(property, "graph") == 0) return AKAOS_WCTYPE_GRAPH;
    if (strcmp(property, "lower") == 0) return AKAOS_WCTYPE_LOWER;
    if (strcmp(property, "print") == 0) return AKAOS_WCTYPE_PRINT;
    if (strcmp(property, "punct") == 0) return AKAOS_WCTYPE_PUNCT;
    if (strcmp(property, "space") == 0) return AKAOS_WCTYPE_SPACE;
    if (strcmp(property, "upper") == 0) return AKAOS_WCTYPE_UPPER;
    if (strcmp(property, "xdigit") == 0) return AKAOS_WCTYPE_XDIGIT;
    return 0;
}

int iswctype(wint_t character, wctype_t property) {
    switch (property) {
        case AKAOS_WCTYPE_ALNUM: return iswalnum(character);
        case AKAOS_WCTYPE_ALPHA: return iswalpha(character);
        case AKAOS_WCTYPE_BLANK:
            return character == ' ' || character == '\t';
        case AKAOS_WCTYPE_CNTRL:
            return character < 0x20U || character == 0x7fU;
        case AKAOS_WCTYPE_DIGIT: return iswdigit(character);
        case AKAOS_WCTYPE_GRAPH:
            return character >= 0x21U && character <= 0x7eU;
        case AKAOS_WCTYPE_LOWER:
            return character >= 'a' && character <= 'z';
        case AKAOS_WCTYPE_PRINT:
            return character >= 0x20U && character <= 0x7eU;
        case AKAOS_WCTYPE_PUNCT:
            return character >= 0x21U && character <= 0x7eU &&
                   !iswalnum(character);
        case AKAOS_WCTYPE_SPACE: return iswspace(character);
        case AKAOS_WCTYPE_UPPER:
            return character >= 'A' && character <= 'Z';
        case AKAOS_WCTYPE_XDIGIT:
            return iswdigit(character) ||
                   (character >= 'a' && character <= 'f') ||
                   (character >= 'A' && character <= 'F');
        default: return 0;
    }
}

int iswblank(wint_t character) {
    return character == ' ' || character == '\t';
}

int iswcntrl(wint_t character) {
    return character < 0x20U || character == 0x7fU;
}

int iswgraph(wint_t character) {
    return character >= 0x21U && character <= 0x7eU;
}

int iswlower(wint_t character) {
    return character >= 'a' && character <= 'z';
}

int iswprint(wint_t character) {
    return character >= 0x20U && character <= 0x7eU;
}

int iswpunct(wint_t character) {
    return iswgraph(character) && !iswalnum(character);
}

int iswupper(wint_t character) {
    return character >= 'A' && character <= 'Z';
}

int iswxdigit(wint_t character) {
    return iswdigit(character) ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

int iswctype_l(wint_t character, wctype_t property, locale_t locale) {
    (void)locale;
    return iswctype(character, property);
}

#define DEFINE_WCTYPE_LOCALE(name) \
    int name##_l(wint_t character, locale_t locale) { \
        (void)locale; return name(character); \
    }
DEFINE_WCTYPE_LOCALE(iswalnum)
DEFINE_WCTYPE_LOCALE(iswalpha)
DEFINE_WCTYPE_LOCALE(iswblank)
DEFINE_WCTYPE_LOCALE(iswcntrl)
DEFINE_WCTYPE_LOCALE(iswdigit)
DEFINE_WCTYPE_LOCALE(iswgraph)
DEFINE_WCTYPE_LOCALE(iswlower)
DEFINE_WCTYPE_LOCALE(iswprint)
DEFINE_WCTYPE_LOCALE(iswpunct)
DEFINE_WCTYPE_LOCALE(iswspace)
DEFINE_WCTYPE_LOCALE(iswupper)
DEFINE_WCTYPE_LOCALE(iswxdigit)
#undef DEFINE_WCTYPE_LOCALE

wint_t towlower_l(wint_t character, locale_t locale) {
    (void)locale;
    return (wint_t)towlower(character);
}

wint_t towupper_l(wint_t character, locale_t locale) {
    (void)locale;
    return (wint_t)towupper(character);
}

int wcscoll(const wchar_t *left, const wchar_t *right) {
    return wcscmp(left, right);
}

size_t wcsxfrm(wchar_t *destination, const wchar_t *source, size_t count) {
    size_t length = wcslen(source);
    if (destination && count != 0U) {
        size_t copied = length < count - 1U ? length : count - 1U;
        wmemcpy(destination, source, copied);
        destination[copied] = L'\0';
    }
    return length;
}

int wcscoll_l(const wchar_t *left, const wchar_t *right, locale_t locale) {
    (void)locale;
    return wcscoll(left, right);
}

size_t wcsxfrm_l(wchar_t *destination, const wchar_t *source, size_t count,
                 locale_t locale) {
    (void)locale;
    return wcsxfrm(destination, source, count);
}

size_t wcsftime(wchar_t *destination, size_t capacity,
                const wchar_t *format, const struct tm *value) {
    if (!destination || !format || !value || capacity == 0U ||
        capacity > (SIZE_MAX / 4U)) return 0U;
    size_t format_length = wcslen(format);
    if (format_length > (SIZE_MAX - 1U) / 4U) return 0U;
    char *narrow_format = (char *)malloc(format_length * 4U + 1U);
    if (!narrow_format) return 0U;
    size_t converted_format = wcstombs(narrow_format, format,
                                       format_length * 4U + 1U);
    if (converted_format == (size_t)-1) {
        free(narrow_format);
        return 0U;
    }
    size_t output_capacity = capacity * 4U;
    char *narrow_output = (char *)malloc(output_capacity);
    if (!narrow_output) {
        free(narrow_format);
        return 0U;
    }
    size_t byte_count = strftime(narrow_output, output_capacity,
                                 narrow_format, value);
    size_t wide_count = 0U;
    if (byte_count != 0U)
        wide_count = mbstowcs(NULL, narrow_output, 0U);
    if (byte_count == 0U || wide_count >= capacity ||
        mbstowcs(destination, narrow_output, capacity) == (size_t)-1) {
        destination[0] = L'\0';
        wide_count = 0U;
    }
    free(narrow_output);
    free(narrow_format);
    return wide_count;
}

static size_t wide_ascii_prefix(const wchar_t *text, char *buffer,
                                size_t capacity) {
    size_t length = 0U;
    while (text && text[length] && length + 1U < capacity &&
           (uint32_t)text[length] < 0x80U) {
        buffer[length] = (char)text[length];
        ++length;
    }
    buffer[length] = '\0';
    return length;
}

long wcstol(const wchar_t *text, wchar_t **endptr, int base) {
    char token[256];
    (void)wide_ascii_prefix(text, token, sizeof(token));
    char *narrow_end = NULL;
    long result = strtol(token, &narrow_end, base);
    if (endptr) *endptr = (wchar_t *)(text + (narrow_end - token));
    return result;
}

unsigned long wcstoul(const wchar_t *text, wchar_t **endptr, int base) {
    char token[256];
    (void)wide_ascii_prefix(text, token, sizeof(token));
    char *narrow_end = NULL;
    unsigned long result = strtoul(token, &narrow_end, base);
    if (endptr) *endptr = (wchar_t *)(text + (narrow_end - token));
    return result;
}

long long wcstoll(const wchar_t *text, wchar_t **endptr, int base) {
    char token[256];
    (void)wide_ascii_prefix(text, token, sizeof(token));
    char *narrow_end = NULL;
    long long result = strtoll(token, &narrow_end, base);
    if (endptr) *endptr = (wchar_t *)(text + (narrow_end - token));
    return result;
}

unsigned long long wcstoull(const wchar_t *text, wchar_t **endptr, int base) {
    char token[256];
    (void)wide_ascii_prefix(text, token, sizeof(token));
    char *narrow_end = NULL;
    unsigned long long result = strtoull(token, &narrow_end, base);
    if (endptr) *endptr = (wchar_t *)(text + (narrow_end - token));
    return result;
}

float wcstof(const wchar_t *text, wchar_t **endptr) {
    char token[256];
    (void)wide_ascii_prefix(text, token, sizeof(token));
    char *narrow_end = NULL;
    float result = strtof(token, &narrow_end);
    if (endptr) *endptr = (wchar_t *)(text + (narrow_end - token));
    return result;
}

double wcstod(const wchar_t *text, wchar_t **endptr) {
    char token[256];
    (void)wide_ascii_prefix(text, token, sizeof(token));
    char *narrow_end = NULL;
    double result = strtod(token, &narrow_end);
    if (endptr) *endptr = (wchar_t *)(text + (narrow_end - token));
    return result;
}

long double wcstold(const wchar_t *text, wchar_t **endptr) {
    char token[256];
    (void)wide_ascii_prefix(text, token, sizeof(token));
    char *narrow_end = NULL;
    long double result = strtold(token, &narrow_end);
    if (endptr) *endptr = (wchar_t *)(text + (narrow_end - token));
    return result;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

size_t strnlen(const char *text, size_t maximum_length) {
    size_t length = 0U;
    if (!text) return 0U;
    while (length < maximum_length && text[length]) ++length;
    return length;
}

size_t strspn(const char *text, const char *accept) {
    size_t length = 0U;
    if (!text || !accept) return 0U;
    while (text[length]) {
        const char *candidate = accept;
        while (*candidate && *candidate != text[length]) ++candidate;
        if (!*candidate) break;
        ++length;
    }
    return length;
}

size_t strcspn(const char *text, const char *reject) {
    size_t length = 0U;
    if (!text || !reject) return 0U;
    while (text[length]) {
        const char *candidate = reject;
        while (*candidate && *candidate != text[length]) ++candidate;
        if (*candidate) break;
        ++length;
    }
    return length;
}

char *strsep(char **stringp, const char *delimiters) {
    if (!stringp || !*stringp || !delimiters) return NULL;

    char *token = *stringp;
    for (char *cursor = token; *cursor; ++cursor) {
        for (const char *delimiter = delimiters; *delimiter; ++delimiter) {
            if (*cursor == *delimiter) {
                *cursor = '\0';
                *stringp = cursor + 1;
                return token;
            }
        }
    }

    *stringp = NULL;
    return token;
}

char *strtok_r(char *text, const char *delimiters, char **save_pointer) {
    if (!delimiters || !save_pointer) {
        errno = EINVAL;
        return NULL;
    }

    char *cursor = text ? text : *save_pointer;
    if (!cursor) return NULL;
    cursor += strspn(cursor, delimiters);
    if (*cursor == '\0') {
        *save_pointer = cursor;
        return NULL;
    }

    char *end = cursor + strcspn(cursor, delimiters);
    if (*end != '\0') {
        *end = '\0';
        *save_pointer = end + 1;
    } else {
        *save_pointer = end;
    }
    return cursor;
}

char *strtok(char *text, const char *delimiters) {
    static char *save_pointer;
    return strtok_r(text, delimiters, &save_pointer);
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strcasecmp(const char *left, const char *right) {
    while (*left && *right) {
        unsigned char left_byte = (unsigned char)*left++;
        unsigned char right_byte = (unsigned char)*right++;
        int difference = tolower(left_byte) - tolower(right_byte);
        if (difference != 0) return difference;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncasecmp(const char *left, const char *right, size_t maximum_length) {
    for (size_t index = 0U; index < maximum_length; ++index) {
        unsigned char left_byte = (unsigned char)left[index];
        unsigned char right_byte = (unsigned char)right[index];
        int difference = tolower(left_byte) - tolower(right_byte);
        if (difference != 0 || left_byte == 0U || right_byte == 0U)
            return difference;
    }
    return 0;
}

int strncmp(const char *left, const char *right, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (left[i] != right[i]) {
            return (unsigned char)left[i] - (unsigned char)right[i];
        }
        if (!left[i]) break;
    }
    return 0;
}

char *strncpy(char *destination, const char *source, size_t n) {
    size_t i;
    for (i = 0; i < n && source[i] != '\0'; ++i) {
        destination[i] = source[i];
    }
    for (; i < n; ++i) {
        destination[i] = '\0';
    }
    return destination;
}

char *strcpy(char *destination, const char *source) {
    char *dest = destination;
    while ((*dest++ = *source++)) ;
    return destination;
}

char *strcat(char *destination, const char *source) {
    char *end = destination + strlen(destination);
    while ((*end++ = *source++)) { }
    return destination;
}

char *strncat(char *destination, const char *source, size_t n) {
    char *end = destination + strlen(destination);
    size_t copied = 0U;
    while (copied < n && source[copied] != '\0') {
        end[copied] = source[copied];
        ++copied;
    }
    end[copied] = '\0';
    return destination;
}

char *strdup(const char *source) {
    if (!source) {
        errno = EINVAL;
        return NULL;
    }
    size_t length = strlen(source);
    if (length == SIZE_MAX) {
        errno = ENOMEM;
        return NULL;
    }
    char *copy = (char *)malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, source, length + 1U);
    return copy;
}

char *strndup(const char *source, size_t maximum_length) {
    if (!source) {
        errno = EINVAL;
        return NULL;
    }
    size_t length = 0U;
    while (length < maximum_length && source[length] != '\0') ++length;
    if (length == SIZE_MAX) {
        errno = ENOMEM;
        return NULL;
    }
    char *copy = (char *)malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, source, length);
    copy[length] = '\0';
    return copy;
}


char *strchr(const char *text, int character) {
    char ch = (char)character;
    while (*text) {
        if (*text == ch) return (char *)text;
        ++text;
    }
    if (ch == '\0') return (char *)text;
    return 0;
}

char *strrchr(const char *text, int character) {
    char ch = (char)character;
    const char *last = 0;
    while (*text) {
        if (*text == ch) last = text;
        ++text;
    }
    if (ch == '\0') return (char *)text;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; ++haystack) {
        if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0) {
            return (char *)haystack;
        }
    }
    return NULL;
}

char *strpbrk(const char *text, const char *accept) {
    if (!text || !accept) return NULL;
    for (; *text; ++text) {
        const char *candidate = accept;
        while (*candidate && *candidate != *text) ++candidate;
        if (*candidate) return (char *)text;
    }
    return NULL;
}

int memcmp(const void *left, const void *right, size_t length) {

    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    for (size_t index = 0; index < length; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return left_bytes[index] - right_bytes[index];
        }
    }
    return 0;
}

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    for (size_t index = 0; index < length; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    unsigned char *d = destination;
    const unsigned char *s = source;
    if (d == s || length == 0) return destination;
    if (d < s) {
        for (size_t i = 0; i < length; ++i) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = length; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    unsigned char *bytes = destination;
    for (size_t index = 0; index < length; ++index) {
        bytes[index] = (unsigned char)value;
    }
    return destination;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == uc) return (void *)(p + i);
    }
    return NULL;
}

uint32_t akaos_crc32(uint32_t crc, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

char *strerror(int errnum) {
    switch (errnum) {
        case 0: return "Success";
        case 1: return "Operation not permitted";
        case 2: return "No such file or directory";
        case 3: return "No such process";
        case 4: return "Interrupted system call";
        case 5: return "Input/output error";
        case 6: return "No such device or address";
        case ENOEXEC: return "Exec format error";
        case 9: return "Bad file descriptor";
        case 10: return "No child processes";
        case 11: return "Resource temporarily unavailable";
        case 12: return "Cannot allocate memory";
        case 13: return "Permission denied";
        case 14: return "Bad address";
        case 16: return "Device or resource busy";
        case 17: return "File exists";
        case EXDEV: return "Cross-device link";
        case 19: return "No such device";
        case 20: return "Not a directory";
        case 21: return "Is a directory";
        case 22: return "Invalid argument";
        case ENFILE: return "Too many open files in system";
        case EMFILE: return "Too many open files";
        case 25: return "Inappropriate ioctl for device";
        case 28: return "No space left on device";
        case ESPIPE: return "Illegal seek";
        case 30: return "Read-only file system";
        case 32: return "Broken pipe";
        case 38: return "Function not implemented";
        case 39: return "Directory not empty";
        case ELOOP: return "Too many levels of symbolic links";
        case 104: return "Connection reset by peer";
        case 107: return "Transport endpoint is not connected";
        case 110: return "Connection timed out";
        case 111: return "Connection refused";
        case 113: return "No route to host";
        default: return "Unknown error";
    }
}


#if defined(__arm__)
void __aeabi_memcpy(void *dest, const void *src, size_t n) {
    memcpy(dest, src, n);
}
void __aeabi_memcpy4(void *dest, const void *src, size_t n) {
    memcpy(dest, src, n);
}
void __aeabi_memcpy8(void *dest, const void *src, size_t n) {
    memcpy(dest, src, n);
}
void __aeabi_memmove(void *dest, const void *src, size_t n) {
    memcpy(dest, src, n);
}
void __aeabi_memmove4(void *dest, const void *src, size_t n) {
    memcpy(dest, src, n);
}
void __aeabi_memmove8(void *dest, const void *src, size_t n) {
    memcpy(dest, src, n);
}
void __aeabi_memset(void *dest, size_t n, int c) {
    memset(dest, c, n);
}
void __aeabi_memset4(void *dest, size_t n, int c) {
    memset(dest, c, n);
}
void __aeabi_memset8(void *dest, size_t n, int c) {
    memset(dest, c, n);
}
void __aeabi_memclr(void *dest, size_t n) {
    memset(dest, 0, n);
}
void __aeabi_memclr4(void *dest, size_t n) {
    memset(dest, 0, n);
}
void __aeabi_memclr8(void *dest, size_t n) {
    memset(dest, 0, n);
}
#endif
