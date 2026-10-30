ENTRY(xscore_entry)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x100000;

    __text_start = .;
    .multiboot : ALIGN(0x1000) { KEEP(*(.multiboot)) } :text
    .text : ALIGN(0x1000) { *(.text .text.*) } :text
    . = ALIGN(0x1000);
    __text_end = .;

    __rodata_start = .;
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) } :rodata
    . = ALIGN(0x1000);
    __rodata_end = .;

    /* Keep the large embedded initramfs out of the low-memory area reserved
     * by UEFI firmware.  The loader uses these physical addresses directly. */
    . = 0x04000000;
    __data_start = .;
    .data : ALIGN(0x1000) { *(.data .data.*) } :data
    . = ALIGN(0x1000);
    __bss_start = .;
    .bss : ALIGN(0x1000) { *(COMMON) *(.bss .bss.*) } :data
    . = ALIGN(0x1000);
    __bss_end = .;

    __data_end = .;
    __kernel_end = .;

    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
