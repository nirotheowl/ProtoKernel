.section ".text.exceptions"

// Exception vector table must be aligned to 2KB
.align 11
.global exception_vectors
exception_vectors:
    // Current EL with SP0
    .align 7
    b sync_exception_sp0        // Synchronous
    .align 7
    b irq_sp0                   // IRQ
    .align 7
    b fiq_sp0                   // FIQ
    .align 7
    b serror_sp0                // SError

    // Current EL with SPx
    .align 7
    b sync_exception_current    // Synchronous
    .align 7
    b irq_current               // IRQ
    .align 7
    b fiq_current               // FIQ
    .align 7
    b serror_current            // SError

    // Lower EL using AArch64
    .align 7
    b sync_exception_lower      // Synchronous
    .align 7
    b irq_lower                 // IRQ
    .align 7
    b fiq_lower                 // FIQ
    .align 7
    b serror_lower              // SError

    // Lower EL using AArch32
    .align 7
    b sync_exception_lower32    // Synchronous
    .align 7
    b irq_lower32               // IRQ
    .align 7
    b fiq_lower32               // FIQ
    .align 7
    b serror_lower32            // SError

// Macro to save context
.macro save_context
    // Allocate space for full exception context (288 bytes)
    sub sp, sp, #288
    
    // Save general purpose registers first
    stp x0, x1, [sp, #32]
    stp x2, x3, [sp, #48]
    stp x4, x5, [sp, #64]
    stp x6, x7, [sp, #80]
    stp x8, x9, [sp, #96]
    stp x10, x11, [sp, #112]
    stp x12, x13, [sp, #128]
    stp x14, x15, [sp, #144]
    stp x16, x17, [sp, #160]
    stp x18, x19, [sp, #176]
    stp x20, x21, [sp, #192]
    stp x22, x23, [sp, #208]
    stp x24, x25, [sp, #224]
    stp x26, x27, [sp, #240]
    stp x28, x29, [sp, #256]
    str x30, [sp, #272]
    
    // Save original SP (before we modified it)
    add x21, sp, #288
    str x21, [sp, #280]
    
    // Now save exception system registers
    mrs x22, elr_el1
    mrs x23, spsr_el1
    mrs x24, esr_el1
    mrs x25, far_el1
    stp x22, x23, [sp, #0]
    stp x24, x25, [sp, #16]
.endm

// Macro to restore context
.macro restore_context
    // Restore exception registers first
    ldp x21, x22, [sp, #0]
    msr elr_el1, x21
    msr spsr_el1, x22
    // ESR and FAR are read-only, don't restore
    
    // Restore general purpose registers
    ldp x0, x1, [sp, #32]
    ldp x2, x3, [sp, #48]
    ldp x4, x5, [sp, #64]
    ldp x6, x7, [sp, #80]
    ldp x8, x9, [sp, #96]
    ldp x10, x11, [sp, #112]
    ldp x12, x13, [sp, #128]
    ldp x14, x15, [sp, #144]
    ldp x16, x17, [sp, #160]
    ldp x18, x19, [sp, #176]
    ldp x20, x21, [sp, #192]
    ldp x22, x23, [sp, #208]
    ldp x24, x25, [sp, #224]
    ldp x26, x27, [sp, #240]
    ldp x28, x29, [sp, #256]
    ldr x30, [sp, #272]
    
    // Restore stack pointer
    add sp, sp, #288
.endm

// Current EL with SP0 handlers
sync_exception_sp0:
    save_context
    mov x0, sp
    bl sync_exception_handler
    restore_context
    eret

irq_sp0:
    save_context
    mov x0, sp
    bl irq_handler
    restore_context
    eret

fiq_sp0:
    save_context
    mov x0, sp
    bl fiq_handler
    restore_context
    eret

serror_sp0:
    save_context
    mov x0, sp
    bl serror_handler
    restore_context
    eret

// Current EL with SPx handlers
sync_exception_current:
    save_context
    mov x0, sp
    bl sync_exception_handler
    restore_context
    eret

irq_current:
    save_context
    mov x0, sp
    bl irq_handler
    restore_context
    eret

fiq_current:
    save_context
    mov x0, sp
    bl fiq_handler
    restore_context
    eret

serror_current:
    save_context
    mov x0, sp
    bl serror_handler
    restore_context
    eret

// Lower EL handlers (simplified for now)
sync_exception_lower:
sync_exception_lower32:
    save_context
    mov x0, sp
    bl sync_exception_handler
    restore_context
    eret

irq_lower:
irq_lower32:
    save_context
    mov x0, sp
    bl irq_handler
    restore_context
    eret

fiq_lower:
fiq_lower32:
    save_context
    mov x0, sp
    bl fiq_handler
    restore_context
    eret

serror_lower:
serror_lower32:
    save_context
    mov x0, sp
    bl serror_handler
    restore_context
    eret

// Function to install exception vectors
.global install_exception_vectors
install_exception_vectors:
    adr x0, exception_vectors
    msr vbar_el1, x0
    isb
    ret