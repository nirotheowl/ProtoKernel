#ifndef _ARCH_TIMER_H
#define _ARCH_TIMER_H

#include <stdint.h>

// RISC-V Timer functions (using SBI timer extension)

static inline uint64_t arch_timer_get_counter(void) {
    uint64_t val;
    __asm__ volatile("rdtime %0" : "=r" (val));
    return val;
}

static inline uint64_t arch_timer_get_frequency(void) {
    // RISC-V typically runs at 10MHz for QEMU
    // This should ideally come from device tree
    return 10000000;
}

static inline void arch_timer_set_compare(uint64_t val) {
    // RISC-V uses SBI call to set timer
    // SBI timer extension (EID 0x54494D45)
    register unsigned long a0 __asm__("a0") = val;
    register unsigned long a7 __asm__("a7") = 0x54494D45;
    register unsigned long a6 __asm__("a6") = 0;
    __asm__ volatile(
        "ecall"
        : "+r"(a0)
        : "r"(a6), "r"(a7)
        : "memory"
    );
}

static inline void arch_timer_enable(void) {
    // Enable timer interrupts in sie register
    uint64_t sie;
    __asm__ volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1 << 5);  // STIE bit
    __asm__ volatile("csrw sie, %0" : : "r"(sie));
}

static inline void arch_timer_disable(void) {
    // Disable timer interrupts in sie register
    uint64_t sie;
    __asm__ volatile("csrr %0, sie" : "=r"(sie));
    sie &= ~(1 << 5);  // Clear STIE bit
    __asm__ volatile("csrw sie, %0" : : "r"(sie));
}

static inline void arch_timer_clear_interrupt(void) {
    // Clear pending timer interrupt
    uint64_t sip;
    __asm__ volatile("csrr %0, sip" : "=r"(sip));
    sip &= ~(1 << 5);  // Clear STIP bit
    __asm__ volatile("csrw sip, %0" : : "r"(sip));
}

static inline void arch_cpu_relax(void) {
    // RISC-V pause instruction (hint)
    __asm__ volatile("pause");
}

#endif /* _ARCH_TIMER_H */