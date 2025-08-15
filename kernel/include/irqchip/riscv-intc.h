/*
 * include/irqchip/riscv-intc.h
 * 
 * RISC-V Local Interrupt Controller (INTC) definitions
 */

#ifndef _IRQCHIP_RISCV_INTC_H
#define _IRQCHIP_RISCV_INTC_H

#include <stdint.h>
#include <irq/irq_domain.h>

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