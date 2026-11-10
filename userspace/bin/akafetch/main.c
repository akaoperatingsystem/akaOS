/*
 * akafetch - akaOS System & Hardware Information Display
 * Displays visual system information, real CPU brand, and active GPU adapter
 *
 * Copyright (c) 2026 akaOS Authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <akaos/version.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_WHITE   "\033[37m"

static void get_proc_field(const char *path, const char *key, char *out, size_t max_len) {
    out[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;

    char buf[4096];
    ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (bytes <= 0) return;
    buf[bytes] = '\0';

    char *line = strtok(buf, "\n");
    while (line) {
        if (strncmp(line, key, strlen(key)) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                strncpy(out, colon, max_len - 1);
                out[max_len - 1] = '\0';
                return;
            }
        }
        line = strtok(NULL, "\n");
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        printf("akafetch %s\n", AKAOS_VERSION);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: akafetch\nDisplay system, kernel, CPU, and GPU specifications with ASCII logo.");
        return 0;
    }

    char cpu_model[128];
    char gpu_model[128];
    char mem_total[64];
    char flags_str[1024];
    char hostname[64];

    get_proc_field("/proc/cpuinfo", "model name", cpu_model, sizeof(cpu_model));
    if (cpu_model[0] == '\0') {
        get_proc_field("/proc/cpuinfo", "Hardware", cpu_model, sizeof(cpu_model));
    }
    if (cpu_model[0] == '\0') {
        strcpy(cpu_model, "Generic x86-64 / Compatible Microprocessor");
    }

    get_proc_field("/proc/cpuinfo", "flags", flags_str, sizeof(flags_str));

    /* Detect Active SIMD Vector Engine */
    const char *simd_engine = "Base FPU";
    if (strstr(flags_str, "amx_tile")) {
        simd_engine = "AMX + AVX-512";
    } else if (strstr(flags_str, "avx512f")) {
        simd_engine = "AVX-512";
    } else if (strstr(flags_str, "avx2")) {
        simd_engine = "AVX2";
    } else if (strstr(flags_str, "avx")) {
        simd_engine = "AVX";
    } else if (strstr(flags_str, "sse4_2")) {
        simd_engine = "SSE4.2";
    } else if (strstr(flags_str, "sve2")) {
        simd_engine = "SVE2";
    } else if (strstr(flags_str, "sve")) {
        simd_engine = "SVE";
    } else if (strstr(flags_str, "asimd") || strstr(flags_str, "neon")) {
        simd_engine = "NEON";
    } else if (strstr(flags_str, "rv64imafdc") || strstr(flags_str, " v ")) {
        simd_engine = "RVV (Vector 1.0)";
    }

    get_proc_field("/proc/gpuinfo", "Chipset", gpu_model, sizeof(gpu_model));
    if (gpu_model[0] == '\0') {
        get_proc_field("/proc/gpuinfo", "Model", gpu_model, sizeof(gpu_model));
    }
    if (gpu_model[0] == '\0') {
        get_proc_field("/proc/gpuinfo", "Driver", gpu_model, sizeof(gpu_model));
    }
    if (gpu_model[0] == '\0') {
        strcpy(gpu_model, "VirtIO-GPU / VESA Linear Framebuffer (/dev/fb0)");
    }

    get_proc_field("/proc/meminfo", "MemTotal", mem_total, sizeof(mem_total));
    if (mem_total[0] == '\0') {
        strcpy(mem_total, "256 MB");
    }

    int host_fd = open("/etc/hostname", O_RDONLY);
    if (host_fd >= 0) {
        ssize_t n = read(host_fd, hostname, sizeof(hostname) - 1);
        close(host_fd);
        if (n > 0) {
            hostname[n] = '\0';
            char *nl = strchr(hostname, '\n');
            if (nl) *nl = '\0';
        } else {
            strcpy(hostname, "akaOS");
        }
    } else {
        strcpy(hostname, "akaOS");
    }

    /* Print neofetch-style ascii and specs */
    printf("\n");
    printf(COLOR_CYAN "   ####      ####   " COLOR_RESET "  " COLOR_BOLD COLOR_GREEN "user" COLOR_WHITE "@" COLOR_CYAN "%s" COLOR_RESET "\n", hostname);
    printf(COLOR_CYAN "  ######    ######  " COLOR_RESET "  " COLOR_WHITE "------------------------" COLOR_RESET "\n");
    printf(COLOR_CYAN " ##    ##  ##    ## " COLOR_RESET "  " COLOR_BOLD COLOR_YELLOW "OS:" COLOR_RESET "        akaOS %s\n", AKAOS_VERSION);
    printf(COLOR_CYAN " ##    ##  ##    ## " COLOR_RESET "  " COLOR_BOLD COLOR_YELLOW "Kernel:" COLOR_RESET "    XSCore %s (SMP)\n", XSCORE_VERSION);
    printf(COLOR_CYAN " ########  ######## " COLOR_RESET "  " COLOR_BOLD COLOR_YELLOW "Shell:" COLOR_RESET "     akash\n");
    printf(COLOR_CYAN " ##    ##  ##    ## " COLOR_RESET "  " COLOR_BOLD COLOR_YELLOW "Display:" COLOR_RESET "   Lumen Compositor (X11 :0)\n");
    printf(COLOR_CYAN " ##    ##  ##    ## " COLOR_RESET "  " COLOR_BOLD COLOR_YELLOW "CPU:" COLOR_RESET "       %s\n", cpu_model);
    printf(COLOR_CYAN " ##    ##  ##    ## " COLOR_RESET "  " COLOR_BOLD COLOR_YELLOW "SIMD / ISA:" COLOR_RESET " %s\n", simd_engine);
    printf(COLOR_CYAN "                    " COLOR_RESET "  " COLOR_BOLD COLOR_YELLOW "GPU:" COLOR_RESET "       %s\n", gpu_model);
    printf(COLOR_CYAN "   ####      ####   " COLOR_RESET "  " COLOR_BOLD COLOR_YELLOW "Memory:" COLOR_RESET "    %s\n", mem_total);
    printf("\n");
    printf("                      " COLOR_BLUE "███" COLOR_CYAN "███" COLOR_GREEN "███" COLOR_YELLOW "███" COLOR_WHITE "███" COLOR_RESET "\n\n");

    return 0;
}
