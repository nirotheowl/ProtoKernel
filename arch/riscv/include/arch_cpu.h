/*
 * arch/riscv/include/arch_cpu.h
 * 
 * RISC-V CPU control operations
 */

#ifndef _ARCH_CPU_H_
#define _ARCH_CPU_H_

#include <stdint.h>

// Enable interrupts
static inline void arch_enable_interrupts(void) {
    __asm__ volatile("csrsi sstatus, 2");  // Set SIE bit
}

// Disable interrupts
static inline void arch_disable_interrupts(void) {
    __asm__ volatile("csrci sstatus, 2");  // Clear SIE bit
}

// Save interrupt state and disable interrupts
static inline uint64_t arch_save_interrupts(void) {
    uint64_t sstatus;
    __asm__ volatile(
        "csrr %0, sstatus\n"
        "csrci sstatus, 2"  // Clear SIE bit
        : "=r"(sstatus)
        :: "memory"
    );
    return sstatus;
}

// Restore interrupt state
static inline void arch_restore_interrupts(uint64_t flags) {
    __asm__ volatile("csrw sstatus, %0" :: "r"(flags) : "memory");
}

// Check if interrupts are enabled
static inline int arch_interrupts_enabled(void) {
    uint64_t sstatus;
    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus));
    // Check if SIE bit (bit 1) is set
    return (sstatus & (1 << 1)) != 0;
}

// Halt the CPU
static inline void arch_halt(void) {
    arch_disable_interrupts();
    while (1) {
        __asm__ volatile("wfi");
    }
}

static inline void arch_cpu_wait(void) {
    __asm__ volatile("wfi");
}

#endif /* _ARCH_CPU_H_ */