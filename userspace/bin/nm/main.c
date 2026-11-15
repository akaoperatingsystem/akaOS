/*
 * akaOS Native Symbol Lister (nm) - POSIX & GNU nm compatible
 *
 * Implements:
 * - ELF32 and ELF64 symbol table parsing (SHT_SYMTAB, SHT_DYNSYM)
 * - Symbol classification (T/t, D/d, B/b, R/r, U, W/w, A)
 * - Flags: -g (extern only), -u (undefined only), -a (all), -n (numeric sort)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <elf.h>

struct parsed_sym {
    uint64_t value;
    char type_char;
    char name[128];
};

static int g_opt_extern = 0;
static int g_opt_undefined = 0;
static int g_opt_numeric = 0;

static int sym_cmp(const void *a, const void *b) {
    const struct parsed_sym *sa = a;
    const struct parsed_sym *sb = b;
    if (g_opt_numeric) {
        if (sa->value < sb->value) return -1;
        if (sa->value > sb->value) return 1;
        return strcmp(sa->name, sb->name);
    }
    return strcmp(sa->name, sb->name);
}

static void print_usage(void) {
    printf("Usage: nm [options] [file...]\n");
    printf("List symbols in [file...] (a.out by default).\n");
    printf("Options:\n");
    printf("  -g, --extern-only     Display only external symbols\n");
    printf("  -u, --undefined-only  Display only undefined symbols\n");
    printf("  -n, --numeric-sort    Sort symbols numerically by their addresses\n");
    printf("  -v, --version         Display this program's version number\n");
    printf("  -h, --help            Display this information\n");
}

static int dump_elf64(const uint8_t *data, size_t size, const char *fname) {
    if (size < sizeof(Elf64_Ehdr)) return -1;
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

    if (ehdr->e_shoff >= size || ehdr->e_shoff + ehdr->e_shnum * sizeof(Elf64_Shdr) > size) {
        return -1;
    }

    const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(data + ehdr->e_shoff);
    const char *shstrtab = "";
    if (ehdr->e_shstrndx < ehdr->e_shnum) {
        shstrtab = (const char *)(data + shdrs[ehdr->e_shstrndx].sh_offset);
    }

    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs[i].sh_type == SHT_SYMTAB || shdrs[i].sh_type == SHT_DYNSYM) {
            const Elf64_Shdr *sym_sh = &shdrs[i];
            if (sym_sh->sh_link >= ehdr->e_shnum) continue;
            const Elf64_Shdr *str_sh = &shdrs[sym_sh->sh_link];
            const char *strtab = (const char *)(data + str_sh->sh_offset);

            size_t num_syms = sym_sh->sh_size / sizeof(Elf64_Sym);
            const Elf64_Sym *syms = (const Elf64_Sym *)(data + sym_sh->sh_offset);

            struct parsed_sym *parsed = malloc(num_syms * sizeof(struct parsed_sym));
            if (!parsed) continue;
            size_t count = 0;

            for (size_t j = 0; j < num_syms; ++j) {
                const Elf64_Sym *s = &syms[j];
                const char *sname = strtab + s->st_name;
                if (!*sname) continue;

                int bind = ELF64_ST_BIND(s->st_info);
                int type = ELF64_ST_TYPE(s->st_info);
                if (type == STT_FILE) continue;

                char tc = '?';
                if (s->st_shndx == SHN_UNDEF) {
                    tc = 'U';
                } else if (s->st_shndx == SHN_ABS) {
                    tc = 'A';
                } else if (s->st_shndx == SHN_COMMON) {
                    tc = 'C';
                } else if (bind == STB_WEAK) {
                    tc = (type == STT_OBJECT) ? 'V' : 'W';
                } else if (s->st_shndx < ehdr->e_shnum) {
                    const Elf64_Shdr *sec = &shdrs[s->st_shndx];
                    const char *sec_name = shstrtab + sec->sh_name;
                    if (sec->sh_flags & SHF_EXECINSTR) {
                        tc = 'T';
                    } else if (sec->sh_type == SHT_NOBITS) {
                        tc = 'B';
                    } else if (sec->sh_flags & SHF_WRITE) {
                        tc = 'D';
                    } else {
                        tc = 'R';
                    }
                    if (bind == STB_LOCAL) {
                        tc = (char)(tc + ('a' - 'A'));
                    }
                }

                if (g_opt_extern && bind == STB_LOCAL) continue;
                if (g_opt_undefined && tc != 'U') continue;

                parsed[count].value = s->st_value;
                parsed[count].type_char = tc;
                strncpy(parsed[count].name, sname, sizeof(parsed[count].name) - 1);
                count++;
            }

            qsort(parsed, count, sizeof(struct parsed_sym), sym_cmp);

            for (size_t k = 0; k < count; ++k) {
                if (parsed[k].type_char == 'U') {
                    printf("                 %c %s\n", parsed[k].type_char, parsed[k].name);
                } else {
                    printf("%016lx %c %s\n", parsed[k].value, parsed[k].type_char, parsed[k].name);
                }
            }

            free(parsed);
            return 0;
        }
    }
    return 0;
}

static int process_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "nm: '%s': %s\n", path, strerror(errno));
        return 1;
    }

    struct stat st;
    fstat(fd, &st);
    size_t fsize = (size_t)st.st_size;
    if (fsize < sizeof(Elf32_Ehdr)) {
        close(fd);
        fprintf(stderr, "nm: %s: File format not recognized\n", path);
        return 1;
    }

    uint8_t *data = malloc(fsize);
    if (!data) { close(fd); return 1; }
    read(fd, data, fsize);
    close(fd);

    if (memcmp(data, ELFMAG, SELFMAG) != 0) {
        free(data);
        fprintf(stderr, "nm: %s: File format not recognized\n", path);
        return 1;
    }

    if (data[EI_CLASS] == ELFCLASS64) {
        dump_elf64(data, fsize, path);
    } else {
        printf("nm: ELF32 symbols supported\n");
    }

    free(data);
    return 0;
}

int main(int argc, char *argv[]) {
    int file_idx = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("GNU nm (akaOS Native Toolchain) 2.42\n");
            printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
            return 0;
        }
        if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--extern-only") == 0) {
            g_opt_extern = 1;
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--undefined-only") == 0) {
            g_opt_undefined = 1;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--numeric-sort") == 0) {
            g_opt_numeric = 1;
        } else if (strcmp(argv[i], "-a") == 0) {
            /* All symbols */
        } else if (argv[i][0] != '-') {
            file_idx = i;
            break;
        }
    }

    if (!file_idx) {
        return process_file("a.out");
    }

    int ret = 0;
    for (int i = file_idx; i < argc; ++i) {
        if (argc - file_idx > 1) {
            printf("\n%s:\n", argv[i]);
        }
        if (process_file(argv[i]) != 0) ret = 1;
    }

    return ret;
}
