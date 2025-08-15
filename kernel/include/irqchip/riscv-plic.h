/*
 * include/irqchip/riscv-plic.h
 * 
 * RISC-V Platform-Level Interrupt Controller (PLIC) definitions
 */

#ifndef _IRQCHIP_RISCV_PLIC_H
#define _IRQCHIP_RISCV_PLIC_H

#include <stdint.h>
#include <irq/irq_domain.h>

// PLIC register offsets
#define PLIC_PRIORITY_BASE      0x000000    // Priority registers
#define PLIC_PENDING_BASE       0x001000    // Interrupt pending bits
#define PLIC_ENABLE_BASE        0x002000    // Interrupt enable bits per context
#define PLIC_CONTEXT_BASE       0x200000    // Context base address

// Per-context register offsets
#define PLIC_CONTEXT_SIZE       0x1000
#define PLIC_ENABLE_OFFSET      0x0000      // Enable bits within context
#define PLIC_THRESHOLD_OFFSET   0x0000      // Priority threshold
#define PLIC_CLAIM_OFFSET       0x0004      // Claim/complete register

// PLIC constants
#define PLIC_MAX_PRIORITY       7
#define PLIC_DEFAULT_PRIORITY   4
#define PLIC_MAX_IRQS           1024
#define PLIC_MAX_CONTEXTS       15872        // Theoretical max

// PLIC data structure
struct plic_data {
    void *base;                    // MMIO base address
    uint32_t nr_irqs;              // Number of interrupts
    uint32_t nr_contexts;          // Number of contexts
    struct irq_domain *domain;     // IRQ domain
};

// Global PLIC instance
extern struct plic_data *plic_primary;

// PLIC operations
int plic_init(void *base, uint32_t nr_irqs, uint32_t nr_contexts);
void plic_mask_irq(uint32_t hwirq);
void plic_unmask_irq(uint32_t hwirq);
void plic_eoi(uint32_t hwirq);
void plic_set_priority(uint32_t hwirq, uint32_t priority);
void plic_set_threshold(uint32_t context, uint32_t threshold);
uint32_t plic_claim(void);
void plic_handle_irq(void);
struct plic_data *plic_get_from_dt(struct device_node *node);

#endif /* _IRQCHIP_RISCV_PLIC_H */