/*
 * kernel/drivers/irqchip/riscv-plic.c
 * 
 * RISC-V Platform-Level Interrupt Controller (PLIC) driver implementation
 */

/* Only compile this driver for RISC-V architecture */
#ifdef __riscv

#include <irqchip/riscv-plic.h>
#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <device/device.h>
#include <device/resource.h>
#include <drivers/driver.h>
#include <drivers/driver_module.h>
#include <drivers/fdt.h>
#include <uart.h>
#include <arch_io.h>
#include <stddef.h>
#include <string.h>

// Global PLIC instance
struct plic_data *plic_primary = NULL;

// Static PLIC data for the primary controller
static struct plic_data primary_plic_data;

// Helper macros for register access
#define plic_read(plic, offset) \
    mmio_read32((uint8_t*)(plic)->base + (offset))
#define plic_write(plic, offset, val) \
    mmio_write32((uint8_t*)(plic)->base + (offset), (val))

// Forward declarations for irq_chip operations
static void plic_irq_mask(struct irq_desc *desc);
static void plic_irq_unmask(struct irq_desc *desc);
static void plic_irq_eoi(struct irq_desc *desc);
static int plic_irq_set_type(struct irq_desc *desc, uint32_t type);

// IRQ chip operations for PLIC
static struct irq_chip plic_chip = {
    .name = "PLIC",
    .irq_mask = plic_irq_mask,
    .irq_unmask = plic_irq_unmask,
    .irq_eoi = plic_irq_eoi,
    .irq_set_type = plic_irq_set_type,
    .irq_enable = plic_irq_unmask,  // Enable is same as unmask
    .irq_disable = plic_irq_mask,   // Disable is same as mask
    .flags = 0,
};

// Forward declarations for domain operations
static int plic_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq);
static void plic_domain_unmap(struct irq_domain *d, uint32_t virq);
static int plic_domain_xlate(struct irq_domain *d, const uint32_t *intspec,
                             uint32_t intsize, uint32_t *out_hwirq, uint32_t *out_type);

// Domain operations for PLIC
static const struct irq_domain_ops plic_domain_ops = {
    .map = plic_domain_map,
    .unmap = plic_domain_unmap,
    .xlate = plic_domain_xlate,
};

// Get context base for a specific context
static inline void* plic_context_base(struct plic_data *plic, uint32_t context) {
    return (uint8_t*)plic->base + PLIC_CONTEXT_BASE + context * PLIC_CONTEXT_SIZE;
}

// Mask an interrupt
void plic_mask_irq(uint32_t hwirq) {
    struct plic_data *plic = plic_primary;
    uint32_t reg, bit;
    
    if (!plic || hwirq == 0 || hwirq > plic->nr_irqs) return;
    
    reg = (hwirq / 32) * 4;
    bit = hwirq % 32;
    
    // Disable for all contexts (for now just use context 1 - S-mode hart 0)
    void *enable_reg = (uint8_t*)plic_context_base(plic, 1) + PLIC_ENABLE_OFFSET + reg;
    uint32_t val = mmio_read32(enable_reg);
    val &= ~(1 << bit);
    mmio_write32(enable_reg, val);
}

// Unmask an interrupt
void plic_unmask_irq(uint32_t hwirq) {
    struct plic_data *plic = plic_primary;
    uint32_t reg, bit;
    
    if (!plic || hwirq == 0 || hwirq > plic->nr_irqs) return;
    
    reg = (hwirq / 32) * 4;
    bit = hwirq % 32;
    
    // Enable for context 1 (S-mode hart 0)
    void *enable_reg = (uint8_t*)plic_context_base(plic, 1) + PLIC_ENABLE_OFFSET + reg;
    uint32_t val = mmio_read32(enable_reg);
    val |= (1 << bit);
    mmio_write32(enable_reg, val);
}

// End of interrupt (complete)
void plic_eoi(uint32_t hwirq) {
    struct plic_data *plic = plic_primary;
    if (!plic) return;
    
    // Write to claim/complete register for context 1
    void *complete_reg = (uint8_t*)plic_context_base(plic, 1) + PLIC_CLAIM_OFFSET;
    mmio_write32(complete_reg, hwirq);
}

// Set interrupt priority
void plic_set_priority(uint32_t hwirq, uint32_t priority) {
    struct plic_data *plic = plic_primary;
    
    if (!plic || hwirq == 0 || hwirq > plic->nr_irqs) return;
    if (priority > PLIC_MAX_PRIORITY) priority = PLIC_MAX_PRIORITY;
    
    plic_write(plic, PLIC_PRIORITY_BASE + hwirq * 4, priority);
}

// Set threshold for a context
void plic_set_threshold(uint32_t context, uint32_t threshold) {
    struct plic_data *plic = plic_primary;
    
    if (!plic || context >= plic->nr_contexts) return;
    if (threshold > PLIC_MAX_PRIORITY) threshold = PLIC_MAX_PRIORITY;
    
    void *threshold_reg = (uint8_t*)plic_context_base(plic, context) + PLIC_THRESHOLD_OFFSET;
    mmio_write32(threshold_reg, threshold);
}

// Claim an interrupt
uint32_t plic_claim(void) {
    struct plic_data *plic = plic_primary;
    if (!plic) return 0;
    
    // Read from claim register for context 1
    void *claim_reg = (uint8_t*)plic_context_base(plic, 1) + PLIC_CLAIM_OFFSET;
    return mmio_read32(claim_reg);
}

// IRQ chip operations implementation
static void plic_irq_mask(struct irq_desc *desc) {
    if (!desc || !desc->domain) return;
    
    struct plic_data *plic = (struct plic_data *)desc->domain->chip_data;
    if (!plic) return;
    
    plic_mask_irq(desc->hwirq);
}

static void plic_irq_unmask(struct irq_desc *desc) {
    if (!desc || !desc->domain) return;
    
    struct plic_data *plic = (struct plic_data *)desc->domain->chip_data;
    if (!plic) return;
    
    plic_unmask_irq(desc->hwirq);
}

static void plic_irq_eoi(struct irq_desc *desc) {
    if (!desc || !desc->domain) return;
    
    struct plic_data *plic = (struct plic_data *)desc->domain->chip_data;
    if (!plic) return;
    
    plic_eoi(desc->hwirq);
}

static int plic_irq_set_type(struct irq_desc *desc, uint32_t type) {
    // PLIC doesn't support trigger type configuration
    // All interrupts are level-triggered
    return 0;
}

// Domain operations implementation
static int plic_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq) {
    struct plic_data *plic = (struct plic_data *)d->chip_data;
    
    if (hwirq == 0 || hwirq > plic->nr_irqs)
        return -1;
    
    // Set chip and handler for this IRQ
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc)
        return -1;
    
    desc->chip = &plic_chip;
    desc->chip_data = plic;
    desc->hwirq = hwirq;
    desc->domain = d;
    
    // Set default priority
    plic_set_priority(hwirq, PLIC_DEFAULT_PRIORITY);
    
    return 0;
}

static void plic_domain_unmap(struct irq_domain *d, uint32_t virq) {
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc)
        return;
    
    // Mask the interrupt
    if (desc->hwirq)
        plic_mask_irq(desc->hwirq);
    
    // Clear descriptor
    desc->chip = NULL;
    desc->chip_data = NULL;
    desc->hwirq = 0;
    desc->domain = NULL;
}

static int plic_domain_xlate(struct irq_domain *d, const uint32_t *intspec,
                             uint32_t intsize, uint32_t *out_hwirq, uint32_t *out_type) {
    // PLIC uses 2-cell interrupt specifier:
    // cell 0: interrupt number
    // cell 1: flags (unused for PLIC)
    if (intsize < 1)
        return -1;
    
    *out_hwirq = intspec[0];
    if (out_type)
        *out_type = IRQ_TYPE_LEVEL_HIGH;  // PLIC is always level-triggered
    
    return 0;
}

// Initialize PLIC
int plic_init(void *base, uint32_t nr_irqs, uint32_t nr_contexts) {
    struct plic_data *plic = &primary_plic_data;
    
    uart_puts("[PLIC] Initializing RISC-V PLIC\n");
    uart_puts("[PLIC] Base: 0x");
    uart_puthex((uint64_t)base);
    uart_puts(", IRQs: ");
    uart_putdec(nr_irqs);
    uart_puts(", Contexts: ");
    uart_putdec(nr_contexts);
    uart_puts("\n");
    
    // Initialize PLIC data structure
    plic->base = base;
    plic->nr_irqs = nr_irqs;
    plic->nr_contexts = nr_contexts;
    
    // Set as primary PLIC
    plic_primary = plic;
    
    // Initialize all interrupts to priority 0 (disabled)
    for (uint32_t i = 1; i <= nr_irqs; i++) {
        plic_set_priority(i, 0);
    }
    
    // Initialize all contexts
    for (uint32_t ctx = 0; ctx < nr_contexts; ctx++) {
        // Disable all interrupts for this context
        void *enable_base = (uint8_t*)plic_context_base(plic, ctx) + PLIC_ENABLE_OFFSET;
        for (uint32_t i = 0; i < (nr_irqs + 31) / 32; i++) {
            mmio_write32((uint8_t*)enable_base + i * 4, 0);
        }
        
        // Set threshold to 0 (allow all priorities)
        plic_set_threshold(ctx, 0);
    }
    
    // Create IRQ domain for PLIC
    plic->domain = irq_domain_create_linear(NULL, nr_irqs, &plic_domain_ops, plic);
    if (!plic->domain) {
        uart_puts("[PLIC] ERROR: Failed to create IRQ domain\n");
        return -1;
    }
    
    uart_puts("[PLIC] PLIC initialized successfully\n");
    return 0;
}

// Handle PLIC interrupt
void plic_handle_irq(void) {
    struct plic_data *plic = plic_primary;
    if (!plic) return;
    
    // Claim the interrupt
    uint32_t hwirq = plic_claim();
    if (hwirq == 0) {
        // No pending interrupt
        return;
    }
    
    // Find virtual IRQ
    uint32_t virq = irq_find_mapping(plic->domain, hwirq);
    if (virq == 0) {
        uart_puts("[PLIC] WARNING: No mapping for hwirq ");
        uart_putdec(hwirq);
        uart_puts("\n");
        plic_eoi(hwirq);
        return;
    }
    
    // Dispatch to handler
    generic_handle_irq(virq);
}

// Get PLIC from device tree (stub for now)
struct plic_data *plic_get_from_dt(struct device_node *node) {
    // TODO: Parse device tree and initialize PLIC
    // For now, just return the primary PLIC if it exists
    return plic_primary;
}

// Driver probe function - check if we can handle this device
static int plic_driver_probe(struct device *dev) {
    if (!dev || !dev->compatible) {
        return PROBE_SCORE_NONE;
    }
    
    // Check for PLIC compatible strings
    if (strstr(dev->compatible, "riscv,plic0") ||
        strstr(dev->compatible, "sifive,plic-1.0.0")) {
        return PROBE_SCORE_EXACT;
    }
    
    return PROBE_SCORE_NONE;
}

// Driver attach function - actually initialize the device
static int plic_driver_attach(struct device *dev) {
    struct resource *res;
    void *base;
    uint32_t nr_irqs = PLIC_MAX_IRQS;  // Default
    uint32_t nr_contexts = 2;  // Default for QEMU
    
    uart_puts("PLIC: Attaching device ");
    uart_puts(dev->name);
    uart_puts("\n");
    
    // Get memory resource
    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res) {
        uart_puts("PLIC: No memory resource found\n");
        return -1;
    }
    
    // Use mapped virtual address if available, otherwise physical
    if (res->mapped_addr) {
        base = res->mapped_addr;
    } else {
        base = (void*)res->start;
    }
    
    // TODO: Get nr_irqs and nr_contexts from device tree properties
    // For now use defaults for QEMU virt machine
    nr_irqs = 96;
    nr_contexts = 2;
    
    return plic_init(base, nr_irqs, nr_contexts);
}

// Driver detach function
static int plic_driver_detach(struct device *dev) {
    (void)dev; // Not supported yet
    return -1;
}

// Driver operations
static const struct driver_ops plic_driver_ops = {
    .probe = plic_driver_probe,
    .attach = plic_driver_attach,
    .detach = plic_driver_detach,
};

// Driver match table
static const struct device_match plic_matches[] = {
    { .type = MATCH_COMPATIBLE, .value = "riscv,plic0" },
    { .type = MATCH_COMPATIBLE, .value = "sifive,plic-1.0.0" },
};

// Driver structure
static struct driver plic_driver = {
    .name = "riscv-plic",
    .class = DRIVER_CLASS_INTC,
    .ops = &plic_driver_ops,
    .matches = plic_matches,
    .num_matches = sizeof(plic_matches) / sizeof(plic_matches[0]),
    .priority = 0,  // Highest priority for interrupt controller
    .flags = DRIVER_FLAG_BUILTIN | DRIVER_FLAG_EARLY,
};

// Driver registration
static void plic_driver_init(void) {
    int ret;
    
    uart_puts("PLIC: Registering driver\n");
    
    ret = driver_register(&plic_driver);
    if (ret == 0) {
        uart_puts("PLIC: Driver registered successfully\n");
    } else {
        uart_puts("PLIC: Failed to register driver\n");
    }
}

// Register as an early IRQCHIP driver
IRQCHIP_DRIVER_MODULE(plic_driver_init, DRIVER_PRIO_EARLY);

#else /* !__riscv */

/* Stub implementations for non-RISC-V architectures */
#include <stdint.h>
#include <stddef.h>

struct device_node;
struct plic_data *plic_primary = NULL;

int plic_init(void *base, uint32_t nr_irqs, uint32_t nr_contexts) { 
    (void)base; (void)nr_irqs; (void)nr_contexts;
    return -1; 
}
void plic_mask_irq(uint32_t hwirq) { (void)hwirq; }
void plic_unmask_irq(uint32_t hwirq) { (void)hwirq; }
void plic_eoi(uint32_t hwirq) { (void)hwirq; }
void plic_set_priority(uint32_t hwirq, uint32_t priority) { (void)hwirq; (void)priority; }
void plic_set_threshold(uint32_t context, uint32_t threshold) { (void)context; (void)threshold; }
uint32_t plic_claim(void) { return 0; }
void plic_handle_irq(void) { }
struct plic_data *plic_get_from_dt(struct device_node *node) { (void)node; return NULL; }

#endif /* __riscv */