ENTRY(_start32)

SECTIONS
{
    . = 0x100000;

    .multiboot : ALIGN(4)
    {
        KEEP(*(.multiboot))
    }

    __text_start = .;
    .text : ALIGN(4096)
    {
        *(.text .text.*)
    }
    __text_end = .;

    __rodata_start = .;
    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    }
    __rodata_end = .;

    __data_start = .;
    .data : ALIGN(4096)
    {
        *(.data .data.*)
    }
    __data_end = .;

    __bss_start = .;
    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss .bss.*)
    }
    __bss_end = .;

    __kernel_end = ALIGN(4096);

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
