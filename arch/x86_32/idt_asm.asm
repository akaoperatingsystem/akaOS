; ============================================================
; akaOS — 32-bit IDT Assembly Helpers
; ============================================================
; ISR stubs for 32-bit protected mode.
; Uses pusha/popa for register saving.

BITS 32
section .text

; ============================================================
; Load IDT
; ============================================================
global idt_load
extern idtp

idt_load:
    lidt [idtp]
    ret

; ============================================================
; ISR Common Stub (32-bit) — for CPU exceptions (INT 0-31)
; ============================================================
extern isr_handler

isr_common_stub:
    pusha               ; Push EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10        ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld
    push esp            ; Argument: pointer to regs struct
    call isr_handler
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds
    popa

    add esp, 8          ; Remove int_no + error_code
    iret

; ============================================================
; ISR Stubs (CPU Exceptions, INT 0-31)
; ============================================================

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0        ; dummy error code
    push dword %1       ; interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    ; error code already on stack
    push dword %1       ; interrupt number
    jmp isr_common_stub
%endmacro

ISR_NOERR 0    ; Divide by Zero
ISR_NOERR 1    ; Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; Breakpoint
ISR_NOERR 4    ; Overflow
ISR_NOERR 5    ; Bound Range Exceeded
ISR_NOERR 6    ; Invalid Opcode
ISR_NOERR 7    ; Device Not Available
ISR_ERR   8    ; Double Fault
ISR_NOERR 9    ; Coprocessor Segment Overrun
ISR_ERR   10   ; Invalid TSS
ISR_ERR   11   ; Segment Not Present
ISR_ERR   12   ; Stack-Segment Fault
ISR_ERR   13   ; General Protection Fault
ISR_ERR   14   ; Page Fault
ISR_NOERR 15   ; Reserved
ISR_NOERR 16   ; x87 FP Exception
ISR_ERR   17   ; Alignment Check
ISR_NOERR 18   ; Machine Check
ISR_NOERR 19   ; SIMD FP Exception
ISR_NOERR 20   ; Virtualization Exception
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30   ; Security Exception
ISR_NOERR 31

; ============================================================
; IRQ Common Stub (for hardware IRQs, INT 32-47)
; ============================================================
extern irq_handler

irq_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld

    extern sysmon_record_irq_start
    extern sysmon_record_irq_end
    call sysmon_record_irq_start

    push esp
    call irq_handler
    add esp, 4

    call sysmon_record_irq_end

    pop gs
    pop fs
    pop es
    pop ds
    popa

    add esp, 8          ; Remove int_no + error_code
    iret

; ============================================================
; IRQ Stubs (IRQ 0-15 → INT 32-47)
; ============================================================

%macro IRQ_STUB 2
global irq%1
irq%1:
    push dword 0        ; Dummy error code
    push dword %2       ; Interrupt number
    jmp irq_common_stub
%endmacro

IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 2, 34
IRQ_STUB 3, 35
IRQ_STUB 4, 36
IRQ_STUB 5, 37
IRQ_STUB 6, 38
IRQ_STUB 7, 39
IRQ_STUB 8, 40
IRQ_STUB 9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

section .note.GNU-stack noalloc noexec nowrite progbits
