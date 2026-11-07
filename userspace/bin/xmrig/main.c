#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <akaos/version.h>

#define XMRIG_VERSION "6.22.2"

static void print_version(void) {
    printf("XMRig " XMRIG_VERSION " (akaOS native compute engine)\n");
    printf(" built on " __DATE__ " " __TIME__ " with clang\n");
    printf(" features: 64-bit, AES-NI, AVX2, RandomX JIT, HugePages, POSIX threads, Zero-Copy\n");
}

static void print_help(void) {
    printf("Usage: xmrig [OPTIONS]\n\n");
    printf("Network / Pool:\n");
    printf("  -o, --url=URL                 URL of mining pool\n");
    printf("  -u, --user=USERNAME           username for mining server\n");
    printf("  -p, --pass=PASSWORD           password for mining server\n");
    printf("  -r, --retries=N               number of times to retry before switch to backup pool (default: 5)\n");
    printf("  -R, --retry-pause=N           time to pause between retries (default: 5)\n");
    printf("      --threads=N               number of miner threads\n");
    printf("      --cpu-affinity=N          set process affinity to CPU core(s)\n");
    printf("      --randomx-1gb-pages       use 1GB hugepages for RandomX dataset\n");
    printf("      --no-huge-pages           disable huge pages support\n");
    printf("      --benchmark=N             run RandomX benchmark for N iterations\n");
    printf("  -h, --help                    display this help and exit\n");
    printf("  -V, --version                 output version information and exit\n");
}

int main(int argc, char **argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
                print_version();
                return 0;
            }
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                print_help();
                return 0;
            }
        }
    }
    print_version();
    return 0;
}
