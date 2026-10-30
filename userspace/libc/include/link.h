#ifndef AKAOS_LIBC_LINK_H
#define AKAOS_LIBC_LINK_H

/* ELF program-header inspection used by libunwind and portable runtimes.
 * Keep the public layout architecture-sized, matching the ELF ABI used by
 * XSCore's native process loader. */
#include <stddef.h>
#include <stdint.h>

#define PT_LOAD 1U
#define PT_PHDR 6U
#define PT_GNU_EH_FRAME 0x6474e550U
#define PT_ARM_EXIDX 0x70000001U

typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf64_Word;
typedef uint16_t Elf64_Half;
typedef uint64_t Elf64_Addr;

typedef struct {
    Elf32_Word p_type;
    Elf32_Addr p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

typedef struct {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Addr p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Addr p_filesz;
    Elf64_Addr p_memsz;
    Elf64_Addr p_align;
} Elf64_Phdr;

#if UINTPTR_MAX > UINT32_MAX
typedef Elf64_Addr Elf_Addr;
typedef Elf64_Half Elf_Half;
typedef Elf64_Phdr Elf_Phdr;
#define ElfW(type) Elf64_##type
#else
typedef Elf32_Addr Elf_Addr;
typedef Elf32_Half Elf_Half;
typedef Elf32_Phdr Elf_Phdr;
#define ElfW(type) Elf32_##type
#endif

struct dl_phdr_info {
    Elf_Addr dlpi_addr;
    const char *dlpi_name;
    const Elf_Phdr *dlpi_phdr;
    Elf_Half dlpi_phnum;
    unsigned long long dlpi_adds;
    unsigned long long dlpi_subs;
    size_t dlpi_tls_modid;
    void *dlpi_tls_data;
};

#ifdef __cplusplus
extern "C" {
#endif

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *),
                    void *data);

#ifdef __cplusplus
}
#endif

#endif
