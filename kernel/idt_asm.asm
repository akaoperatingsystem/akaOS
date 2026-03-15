; ============================================================
; akaOS — 64-bit IDT Assembly Helpers
; ============================================================
; ISR stubs for 64-bit long mode. No pusha/popa in 64-bit,
; so we manually save/restore all general-purpose registers.

BITS 64
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
; ISR Common Stub (64-bit) — for CPU exceptions (INT 0-31)
; ============================================================
extern isr_handler

isr_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld
    mov rdi, rsp
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16     ; Remove int_no + error_code
    iretq

; ============================================================
; ISR Stubs (CPU Exceptions, INT 0-31)
; Some exceptions push an error code, some don't.
; ============================================================

; No error code — push dummy 0
%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0          ; dummy error code
    push %1         ; interrupt number
    jmp isr_common_stub
%endmacro

; CPU pushes error code automatically
%macro ISR_ERR 1
global isr%1
isr%1:
    ; error code already on stack
    push %1         ; interrupt number
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
ISR_NOERR 21   ; Reserved
ISR_NOERR 22   ; Reserved
ISR_NOERR 23   ; Reserved
ISR_NOERR 24   ; Reserved
ISR_NOERR 25   ; Reserved
ISR_NOERR 26   ; Reserved
ISR_NOERR 27   ; Reserved
ISR_NOERR 28   ; Reserved
ISR_NOERR 29   ; Reserved
ISR_ERR   30   ; Security Exception
ISR_NOERR 31   ; Reserved

; ============================================================
; IRQ Common Stub (for hardware IRQs, INT 32-47)
; ============================================================
extern irq_handler

irq_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld

    ; Record IRQ start for System Monitor
    extern sysmon_record_irq_start
    extern sysmon_record_irq_end
    call sysmon_record_irq_start

    ; First argument = pointer to saved registers (stack pointer)
    mov rdi, rsp

    ; Call C handler
    call irq_handler

    ; Record IRQ end for System Monitor
    call sysmon_record_irq_end

    ; Restore all general-purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove interrupt number and error code (2 * 8 bytes)
    add rsp, 16

    ; Return from 64-bit interrupt
    iretq

; ============================================================
; IRQ Stubs (IRQ 0-15 → INT 32-47)
; ============================================================

%macro IRQ_STUB 2
global irq%1
irq%1:
    push 0              ; Dummy error code (8 bytes in 64-bit)
    push %2             ; Interrupt number (8 bytes in 64-bit)
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

; Suppress executable stack warning
section .note.GNU-stack noalloc noexec nowrite progbits
