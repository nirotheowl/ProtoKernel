/*
 * arch/arm64/include/arch_cpu.h
 *
 * ARM64 CPU control operations
 */

#ifndef _ARM64_ARCH_CPU_H_
#define _ARM64_ARCH_CPU_H_

#include <stdint.h>

// Enable interrupts (IRQs only)
static inline void arch_enable_interrupts(void) {
    // Clear I bit (bit 1) in DAIF to enable IRQs
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

// Disable interrupts (IRQs only)
static inline void arch_disable_interrupts(void) {
    // Set I bit (bit 1) in DAIF to disable IRQs
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

// Save interrupt state and disable interrupts
static inline uint64_t arch_save_interrupts(void) {
    uint64_t flags;
    __asm__ volatile(
        "mrs %0, daif\n"
        "msr daifset, #2"
        : "=r" (flags)
        :: "memory"
    );
    return flags;
}

// Restore interrupt state
static inline void arch_restore_interrupts(uint64_t flags) {
    __asm__ volatile("msr daif, %0" :: "r" (flags) : "memory");
}

// Check if interrupts are enabled
static inline int arch_interrupts_enabled(void) {
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r" (daif));
    // Check if I bit (bit 7) is clear
    return !(daif & (1 << 7));
}

// Wait for event (low power wait)
static inline void arch_wait_for_event(void) {
    __asm__ volatile("wfe");
}

// CPU wait/idle (architecture-independent name)
static inline void arch_cpu_wait(void) {
    __asm__ volatile("wfe");
}

// Halt the CPU
static inline void arch_halt(void) {
    arch_disable_interrupts();
    while (1) {
        arch_wait_for_event();
    }
}

#endif // _ARM64_ARCH_CPU_H_