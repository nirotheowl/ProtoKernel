/*
 * arch/riscv/kernel/trap.S
 *
 * RISC-V trap/exception/interrupt handling
 */

.section ".text.trap"

/* Trap vector - must be aligned to 4 bytes */
.align 2
.global trap_vector
trap_vector:
    /* Save context to stack */
    addi sp, sp, -256       /* Allocate stack frame for context */
    
    /* Save all general purpose registers */
    sd ra, 0(sp)
    sd gp, 16(sp)
    sd tp, 24(sp)
    sd t0, 32(sp)
    sd t1, 40(sp)
    sd t2, 48(sp)
    sd s0, 56(sp)
    sd s1, 64(sp)
    sd a0, 72(sp)
    sd a1, 80(sp)
    sd a2, 88(sp)
    sd a3, 96(sp)
    sd a4, 104(sp)
    sd a5, 112(sp)
    sd a6, 120(sp)
    sd a7, 128(sp)
    sd s2, 136(sp)
    sd s3, 144(sp)
    sd s4, 152(sp)
    sd s5, 160(sp)
    sd s6, 168(sp)
    sd s7, 176(sp)
    sd s8, 184(sp)
    sd s9, 192(sp)
    sd s10, 200(sp)
    sd s11, 208(sp)
    sd t3, 216(sp)
    sd t4, 224(sp)
    sd t5, 232(sp)
    sd t6, 240(sp)
    
    /* Save special registers */
    csrr t0, sepc
    sd t0, 248(sp)          /* Save exception PC */
    
    /* Call C trap handler */
    mv a0, sp               /* Pass context pointer as argument */
    csrr a1, scause         /* Pass trap cause */
    csrr a2, stval          /* Pass trap value */
    call riscv_trap_handler
    
    /* Restore special registers */
    ld t0, 248(sp)
    csrw sepc, t0           /* Restore exception PC */
    
    /* Restore general purpose registers */
    ld ra, 0(sp)
    ld gp, 16(sp)
    ld tp, 24(sp)
    ld t0, 32(sp)
    ld t1, 40(sp)
    ld t2, 48(sp)
    ld s0, 56(sp)
    ld s1, 64(sp)
    ld a0, 72(sp)
    ld a1, 80(sp)
    ld a2, 88(sp)
    ld a3, 96(sp)
    ld a4, 104(sp)
    ld a5, 112(sp)
    ld a6, 120(sp)
    ld a7, 128(sp)
    ld s2, 136(sp)
    ld s3, 144(sp)
    ld s4, 152(sp)
    ld s5, 160(sp)
    ld s6, 168(sp)
    ld s7, 176(sp)
    ld s8, 184(sp)
    ld s9, 192(sp)
    ld s10, 200(sp)
    ld s11, 208(sp)
    ld t3, 216(sp)
    ld t4, 224(sp)
    ld t5, 232(sp)
    ld t6, 240(sp)
    
    addi sp, sp, 256        /* Deallocate stack frame */
    sret                    /* Return from trap */

/* Simple trap handler for early boot - just hangs */
.global early_trap_vector
early_trap_vector:
    /* Save some info for debugging */
    csrr t0, scause
    csrr t1, sepc
    csrr t2, stval
    
    /* Infinite loop */
1:  wfi
    j 1b