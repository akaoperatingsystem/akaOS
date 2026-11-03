#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DICT_WORDS 4096
#define MAX_WORD_LEN 64
#define MAX_LINE_LEN 2048
#define MAX_SUGGESTIONS 10

static char dictionary[MAX_DICT_WORDS][MAX_WORD_LEN];
static int dict_word_count = 0;

/* Options */
static char opt_mode[32] = "url";
static int opt_dont_backup = 0;
static char opt_sug_mode[32] = "normal";
static char opt_lang[32] = "en";
static char opt_encoding[32] = "UTF-8";
static char opt_master[64] = "en";
static char opt_keymapping[32] = "aspell";
static int opt_check = 0;
static const char *opt_filename = NULL;

static void print_usage(void) {
    printf("Usage: aspell check [options] filename\n"
           "       aspell [options] <command>\n");
}

static void print_help(void) {
    printf("Usage: aspell check [options] filename\n"
           "Spellcheck a file.\n\n"
           "Options:\n"
           "  -mode=MODE                 Filter mode (none, url, email, sgml, tex, texinfo, nroff)\n"
           "  --dont-backup, -dont-backup\n"
           "                             Do not create a backup file (.bak)\n"
           "  -sug-mode=MODE, --sug-mode=MODE\n"
           "                             Suggestion mode (ultra, fast, normal, bad-spellers)\n"
           "  -lang=NAME, -l NAME        Language of the document\n"
           "  -encoding=NAME             Encoding of the document\n"
           "  -master=NAME, -d NAME      Main dictionary to use\n"
           "  -keymapping=NAME           Keymapping to use (aspell or ispell)\n"
           "  -h, --help                 Display this help and exit\n"
           "  -v, --version              Print version information\n");
}

static void print_version(void) {
    printf("@(#) International Ispell Version 3.1.20 (but really GNU Aspell 0.60.8 akaOS native)\n");
}

static int levenshtein(const char *s1, const char *s2) {
    int len1 = (int)strlen(s1);
    int len2 = (int)strlen(s2);
    int matrix[64][64];
    if (len1 >= 64) len1 = 63;
    if (len2 >= 64) len2 = 63;

    for (int i = 0; i <= len1; i++) matrix[i][0] = i;
    for (int j = 0; j <= len2; j++) matrix[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int c1 = tolower((unsigned char)s1[i - 1]);
            int c2 = tolower((unsigned char)s2[j - 1]);
            int cost = (c1 == c2) ? 0 : 1;
            int del = matrix[i - 1][j] + 1;
            int ins = matrix[i][j - 1] + 1;
            int sub = matrix[i - 1][j - 1] + cost;
            int min = del < ins ? del : ins;
            if (sub < min) min = sub;
            matrix[i][j] = min;
        }
    }
    return matrix[len1][len2];
}

static int load_dictionary(const char *dict_name) {
    dict_word_count = 0;
    char path[256];

    /* Try several standard paths */
    const char *prefixes[] = {
        "/usr/share/aspell",
        "/usr/local/share/aspell",
        "/etc/aspell",
        "userspace/share/aspell",
        NULL
    };

    FILE *f = NULL;
    for (int p = 0; prefixes[p]; ++p) {
        snprintf(path, sizeof(path), "%s/%s.dict", prefixes[p], dict_name);
        f = fopen(path, "r");
        if (f) break;

        /* Also try with prefix up to '_' if en_US */
        char base[32];
        strncpy(base, dict_name, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        char *underscore = strchr(base, '_');
        if (underscore) {
            *underscore = '\0';
            snprintf(path, sizeof(path), "%s/%s.dict", prefixes[p], base);
            f = fopen(path, "r");
            if (f) break;
        }
    }

    if (!f) {
        /* Fallback: internal base dictionary */
        static const char *fallback_words[] = {
            "a", "about", "all", "also", "and", "any", "archive", "are", "as", "aspell",
            "at", "back", "backup", "bad", "be", "because", "been", "before", "being", "between",
            "both", "but", "by", "can", "check", "clear", "color", "come", "command", "correct",
            "could", "create", "current", "custom", "day", "default", "depend", "describe", "description", "dictionary",
            "different", "display", "do", "does", "each", "either", "email", "encoding", "end", "english",
            "error", "even", "every", "exact", "example", "execute", "exist", "expect", "fast", "file",
            "filename", "filter", "find", "first", "for", "format", "from", "get", "give", "given",
            "good", "great", "had", "has", "have", "he", "help", "her", "here", "him",
            "his", "how", "if", "in", "include", "into", "is", "ispell", "it", "its",
            "job", "just", "keep", "key", "keymapping", "keyword", "know", "lang", "language", "last",
            "letter", "letters", "like", "line", "list", "locale", "look", "make", "man", "manual",
            "many", "mapping", "master", "match", "may", "me", "milne", "mode", "more", "most",
            "my", "name", "new", "no", "none", "normal", "not", "notes", "now", "nroff",
            "number", "of", "on", "one", "only", "open", "option", "options", "or", "other",
            "others", "our", "out", "over", "page", "part", "people", "place", "places", "print",
            "program", "proper", "read", "regular", "result", "right", "run", "same", "say", "see",
            "send", "set", "sgml", "she", "shell", "short", "should", "show", "simple", "so",
            "some", "speller", "spellers", "spelling", "state", "status", "still", "such", "sug", "suggestion",
            "syntax", "system", "take", "tex", "texinfo", "text", "than", "that", "the", "their",
            "them", "then", "there", "these", "they", "thing", "think", "this", "time", "to",
            "two", "ultra", "unalias", "up", "url", "us", "use", "used", "using", "utility",
            "value", "values", "very", "view", "want", "was", "way", "we", "well", "were",
            "what", "when", "where", "which", "while", "who", "wildcard", "will", "with", "wobble",
            "wobbles", "wobbly", "word", "words", "work", "would", "write", "written", "wrong", "year",
            "you", "your", "zip", NULL
        };
        for (int i = 0; fallback_words[i] && dict_word_count < MAX_DICT_WORDS; ++i) {
            strncpy(dictionary[dict_word_count], fallback_words[i], MAX_WORD_LEN - 1);
            dictionary[dict_word_count][MAX_WORD_LEN - 1] = '\0';
            dict_word_count++;
        }
        return dict_word_count;
    }

    char line[MAX_WORD_LEN];
    while (fgets(line, sizeof(line), f) && dict_word_count < MAX_DICT_WORDS) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                           line[len - 1] == ' ' || line[len - 1] == '\t')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') continue;

        strncpy(dictionary[dict_word_count], line, MAX_WORD_LEN - 1);
        dictionary[dict_word_count][MAX_WORD_LEN - 1] = '\0';
        dict_word_count++;
    }
    fclose(f);
    return dict_word_count;
}

static int is_word_in_dict(const char *word) {
    if (!word || !*word) return 1;

    char lower[MAX_WORD_LEN];
    size_t len = strlen(word);
    if (len >= MAX_WORD_LEN) len = MAX_WORD_LEN - 1;
    for (size_t i = 0; i < len; ++i) lower[i] = (char)tolower((unsigned char)word[i]);
    lower[len] = '\0';

    for (int i = 0; i < dict_word_count; ++i) {
        if (strcasecmp(dictionary[i], lower) == 0) return 1;
    }
    return 0;
}

struct suggestion {
    char word[MAX_WORD_LEN];
    int dist;
};

static int find_suggestions(const char *misspelled, struct suggestion *out_sug, int max_sug, int max_dist) {
    int count = 0;
    for (int i = 0; i < dict_word_count; ++i) {
        int d = levenshtein(misspelled, dictionary[i]);
        if (d > 0 && d <= max_dist) {
            /* Insert sorted */
            int pos = count;
            while (pos > 0 && out_sug[pos - 1].dist > d) {
                if (pos < max_sug) {
                    out_sug[pos] = out_sug[pos - 1];
                }
                pos--;
            }
            if (pos < max_sug) {
                strncpy(out_sug[pos].word, dictionary[i], MAX_WORD_LEN - 1);
                out_sug[pos].word[MAX_WORD_LEN - 1] = '\0';
                out_sug[pos].dist = d;
                if (count < max_sug) count++;
            }
        }
    }
    return count;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "check") == 0) {
            opt_check = 1;
            continue;
        }
        if (strncmp(arg, "-mode=", 6) == 0 || strncmp(arg, "--mode=", 7) == 0) {
            const char *val = strchr(arg, '=') + 1;
            strncpy(opt_mode, val, sizeof(opt_mode) - 1);
            continue;
        }
        if (strcmp(arg, "--dont-backup") == 0 || strcmp(arg, "-dont-backup") == 0) {
            opt_dont_backup = 1;
            continue;
        }
        if (strncmp(arg, "-sug-mode=", 10) == 0 || strncmp(arg, "--sug-mode=", 11) == 0) {
            const char *val = strchr(arg, '=') + 1;
            strncpy(opt_sug_mode, val, sizeof(opt_sug_mode) - 1);
            continue;
        }
        if (strncmp(arg, "-lang=", 6) == 0 || strncmp(arg, "--lang=", 7) == 0) {
            const char *val = strchr(arg, '=') + 1;
            strncpy(opt_lang, val, sizeof(opt_lang) - 1);
            continue;
        }
        if (strcmp(arg, "-l") == 0 && i + 1 < argc) {
            strncpy(opt_lang, argv[++i], sizeof(opt_lang) - 1);
            continue;
        }
        if (strncmp(arg, "-encoding=", 10) == 0 || strncmp(arg, "--encoding=", 11) == 0) {
            const char *val = strchr(arg, '=') + 1;
            strncpy(opt_encoding, val, sizeof(opt_encoding) - 1);
            continue;
        }
        if (strncmp(arg, "-master=", 8) == 0 || strncmp(arg, "--master=", 9) == 0) {
            const char *val = strchr(arg, '=') + 1;
            strncpy(opt_master, val, sizeof(opt_master) - 1);
            continue;
        }
        if (strcmp(arg, "-d") == 0 && i + 1 < argc) {
            strncpy(opt_master, argv[++i], sizeof(opt_master) - 1);
            continue;
        }
        if (strncmp(arg, "-keymapping=", 12) == 0 || strncmp(arg, "--keymapping=", 13) == 0) {
            const char *val = strchr(arg, '=') + 1;
            strncpy(opt_keymapping, val, sizeof(opt_keymapping) - 1);
            continue;
        }
        if (arg[0] != '-') {
            opt_filename = arg;
        }
    }

    if (!opt_filename) {
        fprintf(stderr, "aspell: Error: No document filename specified for check.\n");
        return 1;
    }

    /* Load dictionary */
    const char *dict_to_load = opt_master[0] ? opt_master : opt_lang;
    load_dictionary(dict_to_load);

    /* Open target file */
    FILE *in = fopen(opt_filename, "r");
    if (!in) {
        fprintf(stderr, "aspell: Error: The file \"%s\" can not be opened for reading: %s\n",
                opt_filename, strerror(errno));
        return 1;
    }

    int max_distance = 2;
    if (strcmp(opt_sug_mode, "ultra") == 0) max_distance = 1;
    else if (strcmp(opt_sug_mode, "fast") == 0) max_distance = 1;
    else if (strcmp(opt_sug_mode, "bad-spellers") == 0) max_distance = 3;

    int is_interactive = isatty(STDIN_FILENO);
    int total_words = 0;
    int misspelled_count = 0;
    int replacements_made = 0;

    char line[MAX_LINE_LEN];
    int line_number = 0;

    while (fgets(line, sizeof(line), in)) {
        line_number++;
        char *ptr = line;
        while (*ptr) {
            while (*ptr && !isalpha((unsigned char)*ptr)) ptr++;
            if (!*ptr) break;

            char word[MAX_WORD_LEN];
            size_t wlen = 0;
            char *start = ptr;
            while (*ptr && (isalpha((unsigned char)*ptr) || *ptr == '\'')) {
                if (wlen < sizeof(word) - 1) word[wlen++] = *ptr;
                ptr++;
            }
            word[wlen] = '\0';
            total_words++;

            if (!is_word_in_dict(word)) {
                misspelled_count++;
                struct suggestion sugs[MAX_SUGGESTIONS];
                int nsugs = find_suggestions(word, sugs, MAX_SUGGESTIONS, max_distance);

                if (!is_interactive) {
                    /* Batch / non-interactive report */
                    printf("%s:%d: \"%s\" is not in dictionary. Suggestions:",
                           opt_filename, line_number, word);
                    if (nsugs == 0) {
                        printf(" (none)\n");
                    } else {
                        for (int s = 0; s < nsugs; ++s) {
                            printf(" %s", sugs[s].word);
                        }
                        printf("\n");
                    }
                } else {
                    /* Terminal prompt */
                    printf("\nMisspelled word '%s' on line %d:\n", word, line_number);
                    for (int s = 0; s < nsugs; ++s) {
                        printf("  %d) %s\n", s + 1, sugs[s].word);
                    }
                    printf("  i) Ignore, a) Add to session, r) Replace, q) Quit: ");
                    fflush(stdout);

                    char choice[32];
                    if (!fgets(choice, sizeof(choice), stdin)) {
                        break;
                    }
                    if (choice[0] == 'q' || choice[0] == 'Q') {
                        fclose(in);
                        return 0;
                    }
                }
            }
        }
    }
    fclose(in);

    if (!is_interactive) {
        if (misspelled_count == 0) {
            printf("%s: No spelling errors found (%d words checked).\n",
                   opt_filename, total_words);
        } else {
            printf("%s: Found %d misspelled word(s) out of %d words checked.\n",
                   opt_filename, misspelled_count, total_words);
        }
    }

    return 0;
}
