// ============================================================
// akaOS — AArch64 Exception Vector Table
// ============================================================
// ARM64 exception vectors must be aligned to 2048 bytes.
// Each vector entry is 128 bytes (32 instructions max).

.section .text
.global vectors
.extern aarch64_irq_handler
.extern aarch64_sync_handler

// Macro for a vector stub that saves registers and calls a handler
.macro VECTOR_ENTRY handler
    // Save x0-x30, SP, ELR_EL1, SPSR_EL1
    sub  sp, sp, #272
    stp  x0,  x1,  [sp, #0]
    stp  x2,  x3,  [sp, #16]
    stp  x4,  x5,  [sp, #32]
    stp  x6,  x7,  [sp, #48]
    stp  x8,  x9,  [sp, #64]
    stp  x10, x11, [sp, #80]
    stp  x12, x13, [sp, #96]
    stp  x14, x15, [sp, #112]
    stp  x16, x17, [sp, #128]
    stp  x18, x19, [sp, #144]
    stp  x20, x21, [sp, #160]
    stp  x22, x23, [sp, #176]
    stp  x24, x25, [sp, #192]
    stp  x26, x27, [sp, #208]
    stp  x28, x29, [sp, #224]
    mrs  x0, elr_el1
    mrs  x1, spsr_el1
    stp  x30, x0,  [sp, #240]
    str  x1,       [sp, #256]

    mov  x0, sp
    bl   \handler

    // Restore
    ldr  x1,       [sp, #256]
    ldp  x30, x0,  [sp, #240]
    msr  spsr_el1, x1
    msr  elr_el1, x0
    ldp  x28, x29, [sp, #224]
    ldp  x26, x27, [sp, #208]
    ldp  x24, x25, [sp, #192]
    ldp  x22, x23, [sp, #176]
    ldp  x20, x21, [sp, #160]
    ldp  x18, x19, [sp, #144]
    ldp  x16, x17, [sp, #128]
    ldp  x14, x15, [sp, #112]
    ldp  x12, x13, [sp, #96]
    ldp  x10, x11, [sp, #80]
    ldp  x8,  x9,  [sp, #64]
    ldp  x6,  x7,  [sp, #48]
    ldp  x4,  x5,  [sp, #32]
    ldp  x2,  x3,  [sp, #16]
    ldp  x0,  x1,  [sp, #0]
    add  sp, sp, #272
    eret
.endm

// Invalid exception — just hang
.macro VECTOR_INVALID
    msr  daifset, #2
1:  wfi
    b    1b
.endm

// ============================================================
// Vector table — 16 entries, each 128 bytes
// ============================================================
.align 11   // 2^11 = 2048 byte alignment
vectors:

// Current EL with SP_EL0
    VECTOR_ENTRY aarch64_sync_handler   // Synchronous
.align 7
    VECTOR_ENTRY aarch64_irq_handler    // IRQ
.align 7
    VECTOR_INVALID                      // FIQ
.align 7
    VECTOR_INVALID                      // SError

// Current EL with SP_ELx
.align 7
    VECTOR_ENTRY aarch64_sync_handler   // Synchronous
.align 7
    VECTOR_ENTRY aarch64_irq_handler    // IRQ
.align 7
    VECTOR_INVALID                      // FIQ
.align 7
    VECTOR_INVALID                      // SError

// Lower EL (AArch64)
.align 7
    VECTOR_ENTRY aarch64_sync_handler
.align 7
    VECTOR_ENTRY aarch64_irq_handler
.align 7
    VECTOR_INVALID
.align 7
    VECTOR_INVALID

// Lower EL (AArch32)
.align 7
    VECTOR_INVALID
.align 7
    VECTOR_INVALID
.align 7
    VECTOR_INVALID
.align 7
    VECTOR_INVALID

.section .note.GNU-stack, "", %progbits
