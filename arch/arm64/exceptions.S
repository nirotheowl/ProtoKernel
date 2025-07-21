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
    // Save x0-x29 and x30 (lr)
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x29, [sp, #-16]!
    str x30, [sp, #-8]!
    
    // Save SP before exception
    mrs x0, sp_el0
    str x0, [sp, #-8]!
    
    // Save exception registers
    mrs x0, elr_el1
    mrs x1, spsr_el1
    mrs x2, esr_el1
    mrs x3, far_el1
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
.endm

// Macro to restore context
.macro restore_context
    // Restore exception registers
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    msr elr_el1, x0
    msr spsr_el1, x1
    // ESR and FAR are read-only, don't restore
    
    // Restore SP before exception
    ldr x0, [sp], #8
    msr sp_el0, x0
    
    // Restore x30 and x0-x29
    ldr x30, [sp], #8
    ldp x28, x29, [sp], #16
    ldp x26, x27, [sp], #16
    ldp x24, x25, [sp], #16
    ldp x22, x23, [sp], #16
    ldp x20, x21, [sp], #16
    ldp x18, x19, [sp], #16
    ldp x16, x17, [sp], #16
    ldp x14, x15, [sp], #16
    ldp x12, x13, [sp], #16
    ldp x10, x11, [sp], #16
    ldp x8, x9, [sp], #16
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
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