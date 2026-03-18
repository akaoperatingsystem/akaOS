; ============================================================
; akaOS 32-bit Boot — Multiboot2 Entry
; ============================================================

BITS 32

; Multiboot2 header
section .multiboot2
align 8
multiboot2_header:
    dd 0xE85250D6              ; Magic
    dd 0                       ; Architecture: i386
    dd multiboot2_header_end - multiboot2_header  ; Header length
    dd -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header)) ; Checksum

    ; Framebuffer tag (request linear framebuffer)
    dw 5            ; Type: framebuffer
    dw 0            ; Flags
    dd 20           ; Size
    dd 1024         ; Width
    dd 768          ; Height
    dd 32           ; Depth
    align 8

    ; End tag
    dw 0            ; Type
    dw 0            ; Flags
    dd 8            ; Size
multiboot2_header_end:

section .bss
align 16
stack_bottom: resb 65536
stack_top:

section .text
global _start
extern kernel_main

_start:
    ; EAX = Multiboot2 magic (0x36D76289)
    ; EBX = pointer to Multiboot2 info struct
    cli
    lgdt [gdt_descriptor]
    jmp 0x08:.reload_cs

.reload_cs:
    mov cx, 0x10
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    mov ss, cx

    mov esp, stack_top

    ; Push Multiboot2 info for kernel_main
    push ebx        ; multiboot2 info pointer
    push eax        ; multiboot2 magic

    call kernel_main

    cli
.hang:
    hlt
    jmp .hang

section .data
align 8
gdt_start:
    ; Null descriptor
    dq 0
    ; Code descriptor: base=0, limit=4GB, code/read/execute, 32-bit
    db 0xFF, 0xFF, 0, 0, 0, 10011010b, 11001111b, 0
    ; Data descriptor: base=0, limit=4GB, data/read/write, 32-bit
    db 0xFF, 0xFF, 0, 0, 0, 10010010b, 11001111b, 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .note.GNU-stack noalloc noexec nowrite progbits
