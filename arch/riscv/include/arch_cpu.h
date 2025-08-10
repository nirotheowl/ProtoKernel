/*
 * arch/riscv/include/arch_cpu.h
 * 
 * RISC-V CPU control operations
 */

#ifndef _ARCH_CPU_H_
#define _ARCH_CPU_H_

static inline void arch_halt(void) {
    while (1) {
        __asm__ volatile("wfi");
    }
}

static inline void arch_enable_interrupts(void) {
    __asm__ volatile("csrsi sstatus, 2");  // Set SIE bit
}

static inline void arch_disable_interrupts(void) {
    __asm__ volatile("csrci sstatus, 2");  // Clear SIE bit
}

static inline void arch_cpu_wait(void) {
    __asm__ volatile("wfi");
}

#endif /* _ARCH_CPU_H_ */