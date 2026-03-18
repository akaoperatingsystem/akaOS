/* ============================================================
 * akaOS — Unix-like Shell (GUI-compatible)
 * ============================================================ */
#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "string.h"
#include "io.h"
#include "arch.h"
#include "fs.h"
#include "time.h"
#include "net.h"

#define INPUT_MAX   256
#define MAX_HISTORY 20
#define MAX_ENV     16
#define ENV_KEY_LEN 32
#define ENV_VAL_LEN 128

/* Command History */
static char history[MAX_HISTORY][INPUT_MAX];
static int  history_count = 0;

/* Environment Variables */
static char env_keys[MAX_ENV][ENV_KEY_LEN];
static char env_vals[MAX_ENV][ENV_VAL_LEN];
static int  env_count = 0;
static int  env_inited = 0;

static void env_init(void) {
    if (env_inited) return;
    strcpy(env_keys[0], "USER");     strcpy(env_vals[0], "root");
    strcpy(env_keys[1], "HOME");     strcpy(env_vals[1], "/home/root");
    strcpy(env_keys[2], "SHELL");    strcpy(env_vals[2], "/bin/akash");
    strcpy(env_keys[3], "HOSTNAME"); strcpy(env_vals[3], "akaOS");
    strcpy(env_keys[4], "PATH");     strcpy(env_vals[4], "/bin:/usr/bin");
    strcpy(env_keys[5], "TERM");     strcpy(env_vals[5], "vga-text");
    env_count = 6;
    env_inited = 1;
}

static const char *env_get(const char *key) {
    for (int i = 0; i < env_count; i++)
        if (strcmp(env_keys[i], key) == 0) return env_vals[i];
    return 0;
}

static void env_set(const char *key, const char *val) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_keys[i], key) == 0) {
            strncpy(env_vals[i], val, ENV_VAL_LEN - 1);
            return;
        }
    }
    if (env_count < MAX_ENV) {
        strncpy(env_keys[env_count], key, ENV_KEY_LEN - 1);
        strncpy(env_vals[env_count], val, ENV_VAL_LEN - 1);
        env_count++;
    }
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* ============================================================
 * Built-in Commands
 * ============================================================ */
static void cmd_help(const char *args) {
    (void)args;
    vga_print_color("\n  File System:\n", VGA_YELLOW, VGA_BLACK);
    vga_print("    ls cd pwd cat touch mkdir rm echo\n");
    vga_print_color("  System:\n", VGA_YELLOW, VGA_BLACK);
    vga_print("    uname whoami hostname uptime date clear reboot\n");
    vga_print_color("  Environment:\n", VGA_YELLOW, VGA_BLACK);
    vga_print("    env export history help\n");
    vga_print_color("  Network:\n", VGA_YELLOW, VGA_BLACK);
    vga_print("    ping <ip>    ifconfig\n\n");
}

static void cmd_ls(const char *args) {
    args = skip_spaces(args);
    fs_node_t *dir = *args ? fs_resolve_path(args) : fs_get_cwd();
    if (!dir) { vga_print_color("ls: not found\n", VGA_LIGHT_RED, VGA_BLACK); return; }
    if (dir->type == FS_FILE) { vga_print(dir->name); vga_print("\n"); return; }
    for (int i = 0; i < dir->child_count; i++) {
        fs_node_t *ch = dir->children[i];
        if (ch->type == FS_DIRECTORY)
            vga_print_color(ch->name, VGA_LIGHT_BLUE, VGA_BLACK);
        else
            vga_print(ch->name);
        vga_print("  ");
    }
    if (dir->child_count) vga_print("\n");
}

static void cmd_cd(const char *args) {
    args = skip_spaces(args);
    if (!*args || strcmp(args,"~")==0) {
        const char *h = env_get("HOME");
        if (h) { fs_node_t *d = fs_resolve_path(h); if (d) fs_set_cwd(d); }
        return;
    }
    fs_node_t *d = fs_resolve_path(args);
    if (!d || d->type != FS_DIRECTORY)
        vga_print_color("cd: not found\n", VGA_LIGHT_RED, VGA_BLACK);
    else
        fs_set_cwd(d);
}

static void cmd_pwd(const char *a) {
    (void)a; char p[256]; fs_get_path(fs_get_cwd(),p,256); vga_print(p); vga_print("\n");
}

static void cmd_cat(const char *args) {
    args = skip_spaces(args);
    if (!*args) { vga_print("Usage: cat <file>\n"); return; }
    fs_node_t *f = fs_resolve_path(args);
    if (!f) { vga_print_color("cat: not found\n", VGA_LIGHT_RED, VGA_BLACK); return; }
    if (f->type != FS_FILE) { vga_print_color("cat: is a directory\n", VGA_LIGHT_RED, VGA_BLACK); return; }
    if (f->size > 0) { vga_print(f->content); if (f->content[f->size-1]!='\n') vga_print("\n"); }
}

static void cmd_touch(const char *args) {
    args = skip_spaces(args);
    if (!*args) { vga_print("Usage: touch <file>\n"); return; }
    if (!fs_resolve_path(args)) fs_create_file(args);
}

static void cmd_mkdir(const char *args) {
    args = skip_spaces(args);
    if (!*args) { vga_print("Usage: mkdir <dir>\n"); return; }
    if (fs_resolve_path(args)) { vga_print_color("mkdir: exists\n", VGA_LIGHT_RED, VGA_BLACK); return; }
    fs_create_dir(args);
}

static void cmd_rm(const char *args) {
    args = skip_spaces(args);
    if (!*args) { vga_print("Usage: rm <path>\n"); return; }
    if (fs_remove(args)) vga_print_color("rm: failed\n", VGA_LIGHT_RED, VGA_BLACK);
}

static void cmd_echo(const char *args) {
    args = skip_spaces(args);
    const char *redir = 0;
    int append = 0;
    for (const char *p = args; *p; p++) { if (*p == '>') { redir = p; break; } }
    if (redir) {
        char text[256]; int tl = (int)(redir - args);
        while (tl > 0 && args[tl-1] == ' ') tl--;
        strncpy(text, args, tl); text[tl] = '\0';
        redir++;
        if (*redir == '>') { append = 1; redir++; }
        while (*redir == ' ') redir++;
        if (!*redir) { vga_print("echo: missing file\n"); return; }
        fs_node_t *f = fs_resolve_path(redir);
        if (!f) f = fs_create_file(redir);
        if (!f || f->type != FS_FILE) return;
        char wb[260]; strcpy(wb, text); strcat(wb, "\n");
        if (append) fs_append(f, wb, strlen(wb)); else fs_write(f, wb, strlen(wb));
        return;
    }
    if (*args) vga_print(args);
    vga_print("\n");
}

static void cmd_uname(const char *args) {
    args = skip_spaces(args);
    if (strcmp(args,"-a")==0) vga_print("akaOS akaOS 1.1 x86_64 akaOS\n");
    else vga_print("akaOS\n");
}
static void cmd_whoami(const char *a) { (void)a; vga_print("root\n"); }
static void cmd_hostname(const char *a) { (void)a; vga_print("akaOS\n"); }
static void cmd_uptime(const char *a) {
    (void)a; char b[64]; timer_format_uptime(b,64); vga_print("up "); vga_print(b); vga_print("\n");
}
static void cmd_date(const char *a) { cmd_uptime(a); }
static void cmd_env(const char *a) {
    (void)a;
    for (int i = 0; i < env_count; i++) {
        vga_print(env_keys[i]); vga_print("="); vga_print(env_vals[i]); vga_print("\n");
    }
}
static void cmd_export(const char *args) {
    args = skip_spaces(args);
    const char *eq = strchr(args, '=');
    if (!eq) { vga_print("Usage: export VAR=val\n"); return; }
    char k[32]; int kl = (int)(eq-args); if(kl>31) kl=31;
    strncpy(k,args,kl); k[kl]='\0'; env_set(k, eq+1);
}
static void cmd_history_show(const char *a) {
    (void)a;
    for (int i = 0; i < history_count; i++) {
        char n[12]; int_to_str(i+1,n); vga_print("  "); vga_print(n);
        vga_print("  "); vga_print(history[i]); vga_print("\n");
    }
}
static void cmd_clear(const char *a) { (void)a; vga_clear(); }
static void cmd_reboot(const char *a) {
    (void)a;
    uint8_t g = 0x02; while (g & 0x02) g = inb(0x64); outb(0x64, 0xFE);
    arch_halt_forever();
}

/* Parse IP from string like "10.0.2.2" */
static uint32_t parse_ip(const char *s) {
    uint8_t o[4] = {0}; int idx = 0;
    for (const char *p = s; *p && idx < 4; p++) {
        if (*p == '.') { idx++; continue; }
        if (*p >= '0' && *p <= '9') o[idx] = o[idx] * 10 + (*p - '0');
    }
    return net_make_ip(o[0], o[1], o[2], o[3]);
}

static void cmd_ping(const char *args) {
    args = skip_spaces(args);
    if (!*args) { vga_print("Usage: ping <ip>\n"); return; }
    if (!net_is_available()) {
        vga_print_color("ping: network not available\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    uint32_t ip = parse_ip(args);
    char ipstr[20]; net_format_ip(ip, ipstr);
    vga_print("PING "); vga_print(ipstr); vga_print("\n");
    for (int i = 0; i < 4; i++) {
        int ms = net_ping(ip, 1000);
        if (ms >= 0) {
            char t[12]; int_to_str(ms, t);
            vga_print("Reply from "); vga_print(ipstr);
            vga_print(": time="); vga_print(t); vga_print("ms\n");
        } else {
            vga_print("Request timed out.\n");
        }
    }
}

static void cmd_ifconfig(const char *a) {
    (void)a;
    if (!net_is_available()) { vga_print("No network interface.\n"); return; }
    vga_print_color("eth0: ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("inet 10.0.2.15  netmask 255.255.255.0  gateway 10.0.2.2\n");
}

/* ============================================================
 * Command Table
 * ============================================================ */
typedef struct { const char *name; void (*handler)(const char *); } command_t;
static const command_t commands[] = {
    {"help",cmd_help},{"ls",cmd_ls},{"cd",cmd_cd},{"pwd",cmd_pwd},
    {"cat",cmd_cat},{"touch",cmd_touch},{"mkdir",cmd_mkdir},{"rm",cmd_rm},
    {"echo",cmd_echo},{"uname",cmd_uname},{"whoami",cmd_whoami},
    {"hostname",cmd_hostname},{"uptime",cmd_uptime},{"date",cmd_date},
    {"env",cmd_env},{"export",cmd_export},{"history",cmd_history_show},
    {"clear",cmd_clear},{"reboot",cmd_reboot},
    {"ping",cmd_ping},{"ifconfig",cmd_ifconfig},
    {0,0}
};

/* ============================================================
 * Exported for GUI terminal
 * ============================================================ */
void shell_execute_one(const char *input) {
    input = skip_spaces(input);
    if (!*input) return;
    /* Add to history */
    if (history_count < MAX_HISTORY)
        strcpy(history[history_count++], input);
    else {
        for (int i = 0; i < MAX_HISTORY-1; i++) strcpy(history[i], history[i+1]);
        strcpy(history[MAX_HISTORY-1], input);
    }
    char cn[64]; const char *args = input; int i = 0;
    while (*args && *args != ' ' && i < 63) cn[i++] = *args++;
    cn[i] = '\0';
    if (*args == ' ') args++;
    for (int c = 0; commands[c].name; c++) {
        if (strcmp(cn, commands[c].name) == 0) { commands[c].handler(args); return; }
    }
    vga_print_color(cn, VGA_LIGHT_RED, VGA_BLACK);
    vga_print(": command not found\n");
}

void shell_print_prompt(void) {
    env_init();
    const char *user = env_get("USER");
    const char *host = env_get("HOSTNAME");
    char path[256]; fs_get_path(fs_get_cwd(), path, 256);
    const char *home = env_get("HOME");
    char tp[256]; const char *dp = path;
    if (home && starts_with(path, home)) {
        tp[0] = '~'; strcpy(tp+1, path + strlen(home));
        dp = tp;
    }
    vga_print_color(user ? user : "user", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print_color("@", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print_color(host ? host : "akaOS", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print_color(":", VGA_WHITE, VGA_BLACK);
    vga_print_color(dp, VGA_LIGHT_BLUE, VGA_BLACK);
    vga_print_color("$ ", VGA_WHITE, VGA_BLACK);
}

int shell_history_count(void) { return history_count; }
const char *shell_history_get(int idx) {
    if (idx >= 0 && idx < history_count) return history[idx];
    return 0;
}

/* Text-mode shell (used when NOT in GUI) */
void shell_run(void) {
    char input[INPUT_MAX];
    env_init();
    while (1) {
        shell_print_prompt();
        /* Inline readline */
        int len = 0, hpos = history_count;
        while (len < INPUT_MAX - 1) {
            char c = keyboard_getchar();
            if (c == '\n') { input[len]='\0'; vga_putchar('\n'); break; }
            if (c == '\b') { if (len > 0) { len--; vga_backspace(); } continue; }
            if (c == (char)0x80) { /* UP */
                if (hpos > 0) { hpos--; while(len>0){vga_backspace();len--;}
                    strcpy(input,history[hpos]); len=strlen(input); vga_print(input); } continue;
            }
            if (c == (char)0x81) { /* DOWN */
                while(len>0){vga_backspace();len--;}
                if(hpos<history_count-1){hpos++;strcpy(input,history[hpos]);len=strlen(input);vga_print(input);}
                else{hpos=history_count;input[0]='\0';} continue;
            }
            if ((unsigned char)c >= 0x80) continue;
            if (c >= 32 && c <= 126) { input[len++] = c; vga_putchar(c); }
        }
        shell_execute_one(input);
    }
}
