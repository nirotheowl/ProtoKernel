/*
 * arch/riscv/boot/boot.S
 * 
 * Description: Simple RISC-V kernel bootstrap code that sets up stack
 *              and jumps to C code for initial testing
 */

.section ".text.boot"

.global _start

/* Import symbols from linker script */
.extern _stack_top
.extern init_riscv

/*
 * Entry point - RISC-V boot
 * QEMU/OpenSBI passes:
 * - a0: hart ID (processor core ID)
 * - a1: pointer to device tree blob (DTB)
 */
_start:
    /* Disable interrupts */
    csrw sie, zero
    
    /* Save boot parameters */
    mv s0, a0  /* Save hart ID */
    mv s1, a1  /* Save DTB pointer */
    
    /* Set up stack pointer */
    la sp, _stack_top
    
    /* Clear BSS section */
    la t0, _bss_start
    la t1, _bss_end
1:
    beq t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:

    /* Jump to C code */
    mv a0, s0  /* Pass hart ID */
    mv a1, s1  /* Pass DTB pointer */
    call init_riscv
    
    /* If init_riscv returns, hang */
3:
    wfi
    j 3b

.section ".data"
.align 3