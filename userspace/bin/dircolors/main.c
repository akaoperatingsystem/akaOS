#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_CAP 1024
#define KEY_CAP 64
#define VALUE_CAP 128
#define ENTRY_CAP 192

struct color_entry { char key[KEY_CAP]; char value[VALUE_CAP]; };
static struct color_entry entries[ENTRY_CAP];
static size_t entry_count;

static const struct color_entry defaults[] = {
    {"NORMAL", "00"}, {"FILE", "00"}, {"DIR", "01;34"},
    {"LINK", "01;36"}, {"ORPHAN", "40;31;01"}, {"MISSING", "00"},
    {"FIFO", "40;33"}, {"SOCK", "01;35"}, {"DOOR", "01;35"},
    {"BLK", "40;33;01"}, {"CHR", "40;33;01"}, {"EXEC", "01;32"},
    {"SETUID", "37;41"}, {"SETGID", "30;43"}, {"CAPABILITY", "00"},
    {"STICKY_OTHER_WRITABLE", "30;42"}, {"OTHER_WRITABLE", "34;42"},
    {"STICKY", "37;44"}, {"*.tar", "01;31"}, {"*.tgz", "01;31"},
    {"*.arc", "01;31"}, {"*.arj", "01;31"}, {"*.taz", "01;31"},
    {"*.lha", "01;31"}, {"*.lz4", "01;31"}, {"*.lzh", "01;31"},
    {"*.lzma", "01;31"}, {"*.tlz", "01;31"}, {"*.txz", "01;31"},
    {"*.tzo", "01;31"}, {"*.t7z", "01;31"}, {"*.zip", "01;31"},
    {"*.z", "01;31"}, {"*.dz", "01;31"}, {"*.gz", "01;31"},
    {"*.lrz", "01;31"}, {"*.lz", "01;31"}, {"*.lzo", "01;31"},
    {"*.xz", "01;31"}, {"*.zst", "01;31"}, {"*.tzst", "01;31"},
    {"*.bz2", "01;31"}, {"*.bz", "01;31"}, {"*.tbz", "01;31"},
    {"*.tbz2", "01;31"}, {"*.tz", "01;31"}, {"*.deb", "01;31"},
    {"*.rpm", "01;31"}, {"*.jar", "01;31"}, {"*.war", "01;31"},
    {"*.ear", "01;31"}, {"*.sar", "01;31"}, {"*.rar", "01;31"},
    {"*.ace", "01;31"}, {"*.zoo", "01;31"}, {"*.cpio", "01;31"},
    {"*.7z", "01;31"}, {"*.rz", "01;31"}, {"*.cab", "01;31"},
    {"*.wim", "01;31"}, {"*.swm", "01;31"}, {"*.dwm", "01;31"},
    {"*.esd", "01;31"}, {"*.jpg", "01;35"}, {"*.jpeg", "01;35"},
    {"*.gif", "01;35"}, {"*.bmp", "01;35"}, {"*.tga", "01;35"},
    {"*.tif", "01;35"}, {"*.tiff", "01;35"}, {"*.png", "01;35"},
    {"*.svg", "01;35"}, {"*.svgz", "01;35"}, {"*.mng", "01;35"},
    {"*.mov", "01;35"}, {"*.mpg", "01;35"}, {"*.mpeg", "01;35"},
    {"*.mkv", "01;35"}, {"*.webm", "01;35"}, {"*.mp4", "01;35"},
    {"*.m4v", "01;35"}, {"*.vob", "01;35"}, {"*.wmv", "01;35"},
    {"*.avi", "01;35"}, {"*.flv", "01;35"}, {"*.ogv", "01;35"},
    {"*.ogg", "00;36"}, {"*.mp3", "00;36"}, {"*.wav", "00;36"},
    {"*.flac", "00;36"}, {"*.m4a", "00;36"}, {"*.aac", "00;36"},
    {"*.opus", "00;36"}, {"*.mid", "00;36"}, {"*.midi", "00;36"},
    {"*.pdf", "00;32"}, {"*.ps", "00;32"}, {"*.eps", "00;32"},
    {"*.doc", "00;32"}, {"*.docx", "00;32"}, {"*.odt", "00;32"},
    {"*.rtf", "00;32"}, {"*.xls", "00;32"}, {"*.xlsx", "00;32"},
    {"*.ods", "00;32"}, {"*.ppt", "00;32"}, {"*.pptx", "00;32"},
    {"*.odp", "00;32"}
};

static void print_help(void) {
    puts("Usage: dircolors [OPTION]... [FILE]\n"
         "Output shell commands to set LS_COLORS for ls and dir.\n\n"
         "  -b, --sh, --bourne-shell  output Bourne shell commands\n"
         "  -c, --csh, --c-shell     output C shell commands\n"
         "  -p, --print-database     output the compiled-in default database\n"
         "      --help               display this help and exit\n"
         "  -v, --version            display version information and exit");
}

static int set_entry(const char *key, const char *value) {
    for (size_t i = 0; i < entry_count; ++i) {
        if (strcmp(entries[i].key, key) == 0) {
            snprintf(entries[i].value, sizeof(entries[i].value), "%s", value);
            return 1;
        }
    }
    if (entry_count >= ENTRY_CAP || strlen(key) >= KEY_CAP ||
        strlen(value) >= VALUE_CAP) return 0;
    strcpy(entries[entry_count].key, key);
    strcpy(entries[entry_count].value, value);
    ++entry_count;
    return 1;
}

static int valid_key_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '*' || c == '?' ||
           c == '[' || c == ']' || c == '-' || c == '.';
}

static int parse_database(FILE *file) {
    char line[LINE_CAP];
    unsigned line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        ++line_number;
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;
        char *key = p;
        while (valid_key_char(*p)) ++p;
        if (p == key) continue;
        if (*p != ' ' && *p != '\t') continue;
        *p++ = '\0';
        while (*p == ' ' || *p == '\t') ++p;
        char *end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) --end;
        *end = '\0';
        if (*p == '\0') continue;
        /* TERM, COLOR, OPTIONS, and EIGHTBIT describe terminal selection, not LS_COLORS entries. */
        if (strcmp(key, "TERM") == 0 || strcmp(key, "COLOR") == 0 ||
            strcmp(key, "OPTIONS") == 0 || strcmp(key, "EIGHTBIT") == 0) continue;
        if (!set_entry(key, p)) {
            fprintf(stderr, "dircolors: too many entries near line %u\n", line_number);
            return 0;
        }
    }
    return !ferror(file);
}

static int initialize_database(void) {
    entry_count = sizeof(defaults) / sizeof(defaults[0]);
    if (entry_count > ENTRY_CAP) return 0;
    for (size_t i = 0; i < entry_count; ++i) entries[i] = defaults[i];
    return 1;
}

static void print_database(void) {
    puts("# Default dircolors database. Lines are COLOR keys followed by SGR codes.");
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); ++i)
        printf("%-26s %s\n", defaults[i].key, defaults[i].value);
}

static void print_escaped_value(const char *value, int csh) {
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '\\' || *p == '"' || *p == '$' || *p == 96 || (csh && *p == '!')) putchar('\\');
        putchar(*p);
    }
}

static int shell_is_csh(void) {
    const char *shell = getenv("SHELL");
    if (!shell) return 0;
    const char *base = strrchr(shell, '/');
    base = base ? base + 1 : shell;
    size_t length = strlen(base);
    return (length >= 3 && strcmp(base + length - 3, "csh") == 0) ||
           (length >= 4 && strcmp(base + length - 4, "tcsh") == 0);
}

static int print_colors(int csh) {
    if (csh) fputs("setenv LS_COLORS \"", stdout);
    else fputs("LS_COLORS=\"", stdout);
    for (size_t i = 0; i < entry_count; ++i) {
        if (i) putchar(':');
        fputs(entries[i].key, stdout);
        putchar('=');
        print_escaped_value(entries[i].value, csh);
    }
    if (csh) puts("\";");
    else puts("\"; export LS_COLORS;");
    return !ferror(stdout);
}

int main(int argc, char **argv) {
    int csh = shell_is_csh();
    int shell_forced = 0;
    int print_database_requested = 0;
    const char *filename = NULL;
    int options_done = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!options_done && strcmp(arg, "--") == 0) { options_done = 1; continue; }
        if (!options_done && (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)) { print_help(); return 0; }
        if (!options_done && (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0)) { puts("dircolors (akaOS coreutils) 1.0"); return 0; }
        if (!options_done && (strcmp(arg, "--print-database") == 0 || strcmp(arg, "-p") == 0)) { print_database_requested = 1; continue; }
        if (!options_done && (strcmp(arg, "--csh") == 0 || strcmp(arg, "--c-shell") == 0 || strcmp(arg, "-c") == 0)) { csh = 1; shell_forced = 1; continue; }
        if (!options_done && (strcmp(arg, "--sh") == 0 || strcmp(arg, "--bourne-shell") == 0 || strcmp(arg, "-b") == 0)) { csh = 0; shell_forced = 1; continue; }
        if (!options_done && arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "dircolors: invalid option '%s'\n", arg);
            return 2;
        }
        if (filename) { fprintf(stderr, "dircolors: too many file operands\n"); return 2; }
        filename = arg;
    }

    (void)shell_forced;
    if (print_database_requested && !filename) { print_database(); return ferror(stdout) ? 1 : 0; }
    if (!initialize_database()) return 1;
    if (filename) {
        FILE *file = fopen(filename, "r");
        if (!file) { fprintf(stderr, "dircolors: %s: %s\n", filename, strerror(errno)); return 2; }
        entry_count = 0;
        int valid = parse_database(file);
        fclose(file);
        if (!valid) return 2;
    }
    return print_colors(csh) ? 0 : 1;
}
