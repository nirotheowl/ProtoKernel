/*
 * arch/arm64/include/arch_cpu.h
 *
 * ARM64 CPU control operations
 */

#ifndef _ARM64_ARCH_CPU_H_
#define _ARM64_ARCH_CPU_H_

// Disable all interrupts
static inline void arch_disable_interrupts(void) {
    __asm__ volatile("msr daifset, #0xf");
}

// Wait for event (low power wait)
static inline void arch_wait_for_event(void) {
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