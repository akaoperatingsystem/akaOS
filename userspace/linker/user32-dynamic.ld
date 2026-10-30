OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;
    .interp : { *(.interp) }
    .text : ALIGN(0x1000) { *(.text.start .text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
    .eh_frame_hdr : ALIGN(4) {
        __eh_frame_hdr_start = .;
        KEEP(*(.eh_frame_hdr))
        __eh_frame_hdr_end = .;
    }
    .eh_frame : ALIGN(8) {
        __eh_frame_start = .;
        KEEP(*(.eh_frame .eh_frame.*))
        __eh_frame_end = .;
    }
    .gcc_except_table : ALIGN(4) { KEEP(*(.gcc_except_table .gcc_except_table.*)) }
    .dynsym : { *(.dynsym) }
    .gnu.hash : { *(.gnu.hash) }
    .hash : { *(.hash) }
    .dynstr : { *(.dynstr) }
    .rela.dyn : { *(.rela.dyn) }
    .rela.plt : { *(.rela.plt) }
    .rel.dyn : { *(.rel.dyn) }
    .rel.plt : { *(.rel.plt) }
    .dynamic : ALIGN(0x1000) { *(.dynamic) }
    .tdata : ALIGN(0x10) { *(.tdata .tdata.* .tdata.rel.ro .tdata.rel.ro.* .gnu.linkonce.td.*) }
    .tbss : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) }
    .data : ALIGN(0x1000) { *(.data .data.*) }
    .bss : ALIGN(0x1000) { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment .note) }
}
