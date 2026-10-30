ENTRY(_start)

/* ARM32 userspace is linked at the address where XSCore maps it. This keeps
 * absolute GOT/data references valid without a dynamic relocator. */
SECTIONS
{
    . = 0x10000000;
    .text : ALIGN(0x1000) { *(.text.start .text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
    .ARM.extab : ALIGN(4) {
        __extab_start = .;
        KEEP(*(.ARM.extab* .gnu.linkonce.armextab.*))
        __extab_end = .;
    }
    .ARM.exidx : ALIGN(4) {
        __exidx_start = .;
        KEEP(*(.ARM.exidx* .gnu.linkonce.armexidx.*))
        __exidx_end = .;
    }
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
    .tdata : ALIGN(0x10) { *(.tdata .tdata.* .tdata.rel.ro .tdata.rel.ro.* .gnu.linkonce.td.*) }
    .tbss : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) }
    .data : ALIGN(0x1000) {
        *(.data .data.*)
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    }
    .bss : ALIGN(0x1000) { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment .note) }
}
