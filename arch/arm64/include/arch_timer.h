#ifndef _ARCH_TIMER_H
#define _ARCH_TIMER_H

#include <stdint.h>

// ARM Generic Timer functions

static inline uint64_t arch_timer_get_counter(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r" (val));
    return val;
}

static inline uint64_t arch_timer_get_frequency(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (val));
    return val;
}

static inline void arch_timer_set_compare(uint64_t val) {
    __asm__ volatile("msr cntp_cval_el0, %0" : : "r" (val));
}

static inline void arch_timer_enable(void) {
    uint32_t val = 1;  // Enable timer
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r" (val));
}

static inline void arch_timer_disable(void) {
    uint32_t val = 2;  // Mask timer (bit 1)
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r" (val));
}

static inline void arch_timer_clear_interrupt(void) {
    // Timer interrupt clears when we write a new compare value
    // or when we disable the timer
    arch_timer_disable();
}

static inline void arch_cpu_relax(void) {
    __asm__ volatile("yield");
}

#endif /* _ARCH_TIMER_H */