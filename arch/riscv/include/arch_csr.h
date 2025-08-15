/*
 * include/arch_csr.h
 * 
 * Architecture-specific CSR operations
 * This header provides abstraction for CSR access on RISC-V
 * On other architectures, these would be no-ops or equivalent operations
 */

#ifndef _ARCH_CSR_H
#define _ARCH_CSR_H

#include <stdint.h>

/* Check for RISC-V architecture */
#ifdef __riscv

// RISC-V interrupt enable bits
#define SIE_SSIE    (1UL << 1)   // Supervisor software interrupt enable
#define SIE_STIE    (1UL << 5)   // Supervisor timer interrupt enable  
#define SIE_SEIE    (1UL << 9)   // Supervisor external interrupt enable

// RISC-V interrupt numbers
#define IRQ_S_SOFT   1
#define IRQ_S_TIMER  5
#define IRQ_S_EXT    9

// CSR operations for interrupt control
static inline uint64_t arch_irq_get_ie(void) {
    uint64_t val;
    __asm__ volatile("csrr %0, sie" : "=r"(val));
    return val;
}

static inline void arch_irq_set_ie(uint64_t val) {
    __asm__ volatile("csrw sie, %0" : : "r"(val));
}

static inline void arch_irq_enable(uint64_t mask) {
    __asm__ volatile("csrs sie, %0" : : "r"(mask));
}

static inline void arch_irq_disable(uint64_t mask) {
    __asm__ volatile("csrc sie, %0" : : "r"(mask));
}

static inline uint64_t arch_irq_get_pending(void) {
    uint64_t val;
    __asm__ volatile("csrr %0, sip" : "=r"(val));
    return val;
}

static inline void arch_irq_clear_pending(uint64_t mask) {
    __asm__ volatile("csrc sip, %0" : : "r"(mask));
}

#else

// Stub implementations for non-RISC-V architectures
#define SIE_SSIE    0
#define SIE_STIE    0
#define SIE_SEIE    0

#define IRQ_S_SOFT   0
#define IRQ_S_TIMER  0
#define IRQ_S_EXT    0

static inline uint64_t arch_irq_get_ie(void) { return 0; }
static inline void arch_irq_set_ie(uint64_t val) { }
static inline void arch_irq_enable(uint64_t mask) { }
static inline void arch_irq_disable(uint64_t mask) { }
static inline uint64_t arch_irq_get_pending(void) { return 0; }
static inline void arch_irq_clear_pending(uint64_t mask) { }

#endif /* __riscv */

#endif /* _ARCH_CSR_H */