/*
 * include/irqchip/riscv-intc.h
 * 
 * RISC-V Local Interrupt Controller (INTC) definitions
 */

#ifndef _IRQCHIP_RISCV_INTC_H
#define _IRQCHIP_RISCV_INTC_H

#include <stdint.h>
#include <irq/irq_domain.h>

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

#else /* !__riscv */

// Stub implementations for non-RISC-V architectures
// These are provided only for compilation compatibility
// Non-RISC-V code should never actually use these

#define SIE_SSIE    0
#define SIE_STIE    0
#define SIE_SEIE    0

#define IRQ_S_SOFT   0
#define IRQ_S_TIMER  0
#define IRQ_S_EXT    0

static inline uint64_t arch_irq_get_ie(void) { return 0; }
static inline void arch_irq_set_ie(uint64_t val) { (void)val; }
static inline void arch_irq_enable(uint64_t mask) { (void)mask; }
static inline void arch_irq_disable(uint64_t mask) { (void)mask; }
static inline uint64_t arch_irq_get_pending(void) { return 0; }
static inline void arch_irq_clear_pending(uint64_t mask) { (void)mask; }

#endif /* __riscv */

// INTC data structure
struct intc_data {
    struct irq_domain *domain;     // IRQ domain for local interrupts
};

// Global INTC instance
extern struct intc_data *intc_primary;

// INTC operations
int intc_init(void);
void intc_handle_irq(uint64_t cause);
void intc_enable_external(void);
void intc_disable_external(void);

#endif /* _IRQCHIP_RISCV_INTC_H */