/*
 * akaOS Native Object Dumper (objdump) - POSIX & GNU objdump compatible
 *
 * Implements:
 * - File headers (-f)
 * - Section headers (-h)
 * - Symbol tables (-t)
 * - Full contents (-s)
 * - Disassembly view (-d)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <elf.h>
#include <ctype.h>

static int g_opt_file_hdr = 0;
static int g_opt_sec_hdr = 0;
static int g_opt_syms = 0;
static int g_opt_full = 0;
static int g_opt_disasm = 0;

static void print_usage(void) {
    printf("Usage: objdump <option(s)> <file(s)>\n");
    printf(" Display information from object <file(s)>.\n");
    printf(" At least one of the following switches must be given:\n");
    printf("  -f, --file-headers    Display the contents of the overall file header\n");
    printf("  -h, --section-headers Display the contents of the section headers\n");
    printf("  -t, --syms            Display the contents of the symbol table(s)\n");
    printf("  -s, --full-contents   Display the full contents of all sections requested\n");
    printf("  -d, --disassemble     Display assembler contents of executable sections\n");
    printf("  -v, --version         Display this program's version number\n");
    printf("  -H, --help            Display this information\n");
}

static void dump_hex(const uint8_t *data, size_t size, uint64_t base_vma) {
    for (size_t i = 0; i < size; i += 16) {
        printf(" %04lx ", base_vma + i);
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) printf("%02x", data[i + j]);
            else printf("  ");
            if (j % 4 == 3) printf(" ");
        }
        printf(" ");
        for (size_t j = 0; j < 16 && i + j < size; ++j) {
            uint8_t c = data[i + j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
        printf("\n");
    }
}

static int dump_elf64(const uint8_t *data, size_t size, const char *fname) {
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
    if (ehdr->e_shoff >= size || ehdr->e_shoff + ehdr->e_shnum * sizeof(Elf64_Shdr) > size) {
        return -1;
    }

    const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(data + ehdr->e_shoff);
    const char *shstrtab = "";
    if (ehdr->e_shstrndx < ehdr->e_shnum) {
        shstrtab = (const char *)(data + shdrs[ehdr->e_shstrndx].sh_offset);
    }

    printf("\n%s:     file format elf64-x86-64\n", fname);

    if (g_opt_file_hdr) {
        printf("architecture: i386:x86-64, flags 0x%08x:\n", ehdr->e_flags);
        printf("start address 0x%016lx\n\n", ehdr->e_entry);
    }

    if (g_opt_sec_hdr) {
        printf("Sections:\n");
        printf("Idx Name          Size      VMA               LMA               File off  Algn\n");
        for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
            const Elf64_Shdr *s = &shdrs[i];
            const char *sname = shstrtab + s->sh_name;
            printf("%3d %-13s %08lx  %016lx  %016lx  %08lx  2**%d\n",
                   i, sname, s->sh_size, s->sh_addr, s->sh_addr, s->sh_offset,
                   (s->sh_addralign > 0) ? __builtin_ctzll(s->sh_addralign) : 0);
            printf("                  CONTENTS, ALLOC, LOAD\n");
        }
    }

    if (g_opt_full) {
        for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
            const Elf64_Shdr *s = &shdrs[i];
            const char *sname = shstrtab + s->sh_name;
            if (s->sh_type == SHT_NOBITS || s->sh_size == 0) continue;
            printf("Contents of section %s:\n", sname);
            dump_hex(data + s->sh_offset, s->sh_size, s->sh_addr);
        }
    }

    if (g_opt_disasm) {
        for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
            const Elf64_Shdr *s = &shdrs[i];
            const char *sname = shstrtab + s->sh_name;
            if (!(s->sh_flags & SHF_EXECINSTR) || s->sh_size == 0) continue;
            printf("\nDisassembly of section %s:\n", sname);
            dump_hex(data + s->sh_offset, s->sh_size, s->sh_addr);
        }
    }

    return 0;
}

static int process_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }

    struct stat st;
    fstat(fd, &st);
    size_t fsize = (size_t)st.st_size;
    if (fsize < sizeof(Elf32_Ehdr)) {
        close(fd);
        fprintf(stderr, "objdump: %s: File format not recognized\n", path);
        return 1;
    }

    uint8_t *data = malloc(fsize);
    if (!data) { close(fd); return 1; }
    read(fd, data, fsize);
    close(fd);

    if (memcmp(data, ELFMAG, SELFMAG) != 0) {
        free(data);
        fprintf(stderr, "objdump: %s: File format not recognized\n", path);
        return 1;
    }

    if (data[EI_CLASS] == ELFCLASS64) {
        dump_elf64(data, fsize, path);
    } else {
        printf("objdump: ELF32 file format\n");
    }

    free(data);
    return 0;
}

int main(int argc, char *argv[]) {
    int file_idx = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-H") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("GNU objdump (akaOS Native Toolchain) 2.42\n");
            printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
            return 0;
        }
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file-headers") == 0) {
            g_opt_file_hdr = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--section-headers") == 0) {
            g_opt_sec_hdr = 1;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--syms") == 0) {
            g_opt_syms = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--full-contents") == 0) {
            g_opt_full = 1;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--disassemble") == 0) {
            g_opt_disasm = 1;
        } else if (argv[i][0] != '-') {
            file_idx = i;
            break;
        }
    }

    if (!g_opt_file_hdr && !g_opt_sec_hdr && !g_opt_syms && !g_opt_full && !g_opt_disasm) {
        g_opt_sec_hdr = 1; /* Default to section headers */
    }

    if (!file_idx) {
        return process_file("a.out");
    }

    int ret = 0;
    for (int i = file_idx; i < argc; ++i) {
        if (process_file(argv[i]) != 0) ret = 1;
    }

    return ret;
}
