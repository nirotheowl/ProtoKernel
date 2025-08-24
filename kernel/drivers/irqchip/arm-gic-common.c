/*
 * kernel/drivers/irqchip/arm-gic-common.c
 * 
 * ARM Generic Interrupt Controller (GIC) common code
 * Handles probe, dispatch, and shared functionality
 */

#ifdef __aarch64__

#include <irqchip/arm-gic.h>
#include <irq/irq.h>
#include <device/device.h>
#include <device/resource.h>
#include <drivers/driver.h>
#include <drivers/driver_module.h>
#include <uart.h>
#include <arch_io.h>
#include <stddef.h>
#include <string.h>

// Global GIC instance
struct gic_data *gic_primary = NULL;

// Static GIC data for the primary controller
static struct gic_data primary_gic_data;

// Forward declarations for irq_chip operations
static void gic_irq_mask(struct irq_desc *desc);
static void gic_irq_unmask(struct irq_desc *desc);
static void gic_irq_eoi(struct irq_desc *desc);
static int gic_irq_set_type(struct irq_desc *desc, uint32_t type);

// IRQ chip operations - common wrapper for all GIC versions
static struct irq_chip gic_chip = {
    .name = "GIC",
    .irq_mask = gic_irq_mask,
    .irq_unmask = gic_irq_unmask,
    .irq_eoi = gic_irq_eoi,
    .irq_set_type = gic_irq_set_type,
    .irq_enable = gic_irq_unmask,  // Enable is same as unmask
    .irq_disable = gic_irq_mask,   // Disable is same as mask
    .flags = 0,
};

// Forward declarations for domain operations
static int gic_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq);
static void gic_domain_unmap(struct irq_domain *d, uint32_t virq);
static int gic_domain_xlate(struct irq_domain *d, const uint32_t *intspec,
                            uint32_t intsize, uint32_t *out_hwirq, uint32_t *out_type);

// Domain operations for GIC
static const struct irq_domain_ops gic_domain_ops = {
    .map = gic_domain_map,
    .unmap = gic_domain_unmap,
    .xlate = gic_domain_xlate,
};

// IRQ chip operation wrappers that dispatch to version-specific implementations
static void gic_irq_mask(struct irq_desc *desc) {
    if (desc && gic_primary && gic_primary->ops) {
        gic_primary->ops->mask_irq(gic_primary, desc->hwirq);
    }
}

static void gic_irq_unmask(struct irq_desc *desc) {
    if (desc && gic_primary && gic_primary->ops) {
        gic_primary->ops->unmask_irq(gic_primary, desc->hwirq);
    }
}

static void gic_irq_eoi(struct irq_desc *desc) {
    if (desc && gic_primary && gic_primary->ops) {
        gic_primary->ops->eoi(gic_primary, desc->hwirq);
    }
}

static int gic_irq_set_type(struct irq_desc *desc, uint32_t type) {
    if (!desc || !gic_primary || !gic_primary->ops) return -1;
    
    uint32_t config = 0;
    if (type & IRQ_TYPE_EDGE_RISING) {
        config = 0x2;  // Edge-triggered
    }
    // Level-triggered is 0x0 (default)
    
    gic_primary->ops->set_config(gic_primary, desc->hwirq, config);
    return 0;
}

// Domain operations implementation
static int gic_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq) {
    struct gic_data *gic = (struct gic_data *)d->chip_data;
    
    // Check hwirq is within valid range
    if (hwirq >= gic->nr_irqs) {
        return -1;  // Invalid hwirq
    }
    
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc) return -1;
    
    // Set the chip and handler
    desc->chip = &gic_chip;
    desc->chip_data = gic;
    
    // Set default priority and target/affinity
    if (gic->ops && gic->ops->set_priority) {
        gic->ops->set_priority(gic, hwirq, GIC_PRIORITY_DEFAULT);
    }
    if (gic->ops && gic->ops->set_target) {
        gic->ops->set_target(gic, hwirq, 0x1);  // Default target
    }
    
    return 0;
}

static void gic_domain_unmap(struct irq_domain *d, uint32_t virq) {
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc) return;
    
    // Mask the interrupt
    if (gic_primary && gic_primary->ops && gic_primary->ops->mask_irq) {
        gic_primary->ops->mask_irq(gic_primary, desc->hwirq);
    }
    
    // Clear chip data
    desc->chip = NULL;
    desc->chip_data = NULL;
}

static int gic_domain_xlate(struct irq_domain *d, const uint32_t *intspec,
                            uint32_t intsize, uint32_t *out_hwirq, uint32_t *out_type) {
    // GIC uses 3-cell interrupt specifier:
    // cell 0: interrupt type (0=SPI, 1=PPI)
    // cell 1: interrupt number
    // cell 2: flags (trigger type, etc.)
    
    if (intsize < 3) return -1;
    
    uint32_t type = intspec[0];
    uint32_t irq = intspec[1];
    uint32_t flags = intspec[2];
    
    // Calculate hardware IRQ number
    if (type == 0) {
        *out_hwirq = irq + GIC_SPI_BASE;
    } else if (type == 1) {
        *out_hwirq = irq + GIC_PPI_BASE;
    } else {
        return -1;
    }
    
    // Extract trigger type from flags
    *out_type = flags & 0xF;
    
    return 0;
}

// Create IRQ domain for GIC
struct irq_domain *gic_create_domain(struct gic_data *gic) {
    struct irq_domain *domain;
    
    // Create a linear domain for the GIC
    domain = irq_domain_create_linear(NULL, gic->nr_irqs,
                                      &gic_domain_ops, gic);
    if (!domain) {
        uart_puts("GIC: Failed to create IRQ domain\n");
        return NULL;
    }
    
    // Set the chip for the domain
    domain->chip = &gic_chip;
    domain->chip_data = gic;
    
    gic->domain = domain;
    return domain;
}

// Public interface functions that dispatch to version-specific implementations
void gic_mask_irq(uint32_t hwirq) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->mask_irq) {
        gic_primary->ops->mask_irq(gic_primary, hwirq);
    }
}

void gic_unmask_irq(uint32_t hwirq) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->unmask_irq) {
        gic_primary->ops->unmask_irq(gic_primary, hwirq);
    }
}

void gic_eoi(uint32_t hwirq) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->eoi) {
        gic_primary->ops->eoi(gic_primary, hwirq);
    }
}

uint32_t gic_acknowledge_irq(void) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->acknowledge_irq) {
        return gic_primary->ops->acknowledge_irq(gic_primary);
    }
    return GIC_SPURIOUS_IRQ;
}

void gic_set_priority(uint32_t hwirq, uint8_t priority) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->set_priority) {
        gic_primary->ops->set_priority(gic_primary, hwirq, priority);
    }
}

void gic_set_target(uint32_t hwirq, uint8_t cpu_mask) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->set_target) {
        gic_primary->ops->set_target(gic_primary, hwirq, cpu_mask);
    }
}

void gic_set_config(uint32_t hwirq, uint32_t config) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->set_config) {
        gic_primary->ops->set_config(gic_primary, hwirq, config);
    }
}

void gic_send_sgi(uint32_t sgi_id, uint32_t target_mask) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->send_sgi) {
        gic_primary->ops->send_sgi(gic_primary, sgi_id, target_mask);
    }
}

void gic_enable(void) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->enable) {
        gic_primary->ops->enable(gic_primary);
    }
}

void gic_disable(void) {
    if (gic_primary && gic_primary->ops && gic_primary->ops->disable) {
        gic_primary->ops->disable(gic_primary);
    }
}

// Initialize GIC (called after probe)
int gic_init(void) {
    if (!gic_primary) {
        uart_puts("GIC: No GIC controller found - probe must be called first\n");
        return -1;
    }
    
    uart_puts("GIC: Enabling interrupt controller\n");
    gic_enable();
    
    return 0;
}

// Probe GIC from device
int gic_probe(struct device *dev) {
    struct gic_data *gic;
    struct resource *res;
    
    uart_puts("GIC: Probing device\n");
    
    // Check if we've already initialized a GIC
    if (gic_primary) {
        uart_puts("GIC: Already initialized, skipping probe\n");
        return -1;
    }
    
    // Use the static structure for this probe attempt
    gic = &primary_gic_data;
    
    // Only clear if not already in use (defensive check)
    if (gic == gic_primary) {
        uart_puts("GIC: ERROR: primary_gic_data already in use!\n");
        return -1;
    }
    
    // Clear the structure to ensure clean state
    memset(gic, 0, sizeof(*gic));
    
    // Determine GIC version from compatible string
    if (strstr(dev->compatible, "arm,gic-v3")) {
        gic->version = GIC_V3;
        gic->ops = &gicv3_ops;
        uart_puts("GIC: Detected GICv3\n");
    } else if (strstr(dev->compatible, "arm,gic-400") ||
               strstr(dev->compatible, "arm,cortex-a15-gic") ||
               strstr(dev->compatible, "arm,cortex-a9-gic")) {
        gic->version = GIC_V2;
        gic->ops = &gicv2_ops;
        uart_puts("GIC: Detected GICv2\n");
    } else {
        uart_puts("GIC: Unknown GIC version\n");
        return -1;
    }
    
    // Get distributor base address
    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res) {
        uart_puts("GIC: Failed to get distributor resource\n");
        return -1;
    }
    if (!res->mapped_addr) {
        uart_puts("GIC: Distributor not mapped to virtual memory\n");
        return -1;
    }
    gic->dist_base = res->mapped_addr;
    
    // Get CPU interface base address (GICv2) or redistributor (GICv3)
    if (gic->version == GIC_V2) {
        res = device_get_resource(dev, RES_TYPE_MEM, 1);
        if (!res) {
            uart_puts("GIC: Failed to get CPU interface resource\n");
            return -1;
        }
        if (!res->mapped_addr) {
            uart_puts("GIC: CPU interface not mapped to virtual memory\n");
            return -1;
        }
        gic->cpu_base = res->mapped_addr;
    } else if (gic->version == GIC_V3) {
        // GICv3 uses redistributor regions instead of CPU interface
        res = device_get_resource(dev, RES_TYPE_MEM, 1);
        if (!res) {
            uart_puts("GIC: Failed to get redistributor resource\n");
            return -1;
        }
        if (!res->mapped_addr) {
            uart_puts("GIC: Redistributor not mapped to virtual memory\n");
            return -1;
        }
        gic->redist_base = res->mapped_addr;
        gic->redist_size = resource_size(res);
        uart_puts("GIC: Redistributor mapped at ");
        uart_puthex((uint64_t)gic->redist_base);
        uart_puts(" size ");
        uart_puthex(gic->redist_size);
        uart_puts("\n");
    }
    
    gic->dev = dev;
    
    // Initialize version-specific implementation
    if (gic->ops && gic->ops->init) {
        if (gic->ops->init(gic) != 0) {
            uart_puts("GIC: Version-specific initialization failed\n");
            return -1;
        }
    }
    
    // Create IRQ domain
    if (!gic_create_domain(gic)) {
        uart_puts("GIC: Failed to create domain\n");
        return -1;
    }
    
    // Set as primary GIC
    gic_primary = gic;
    
    // Set the default domain
    irq_set_default_domain(gic->domain);
    
    uart_puts("GIC: Probe complete\n");
    return 0;
}

// Driver operations
static int gic_driver_probe(struct device *dev) {
    // Check if this is a GIC device
    if (!dev || !dev->compatible) {
        return PROBE_SCORE_NONE;
    }
    
    // Check for GICv2 compatible strings
    if (strstr(dev->compatible, "arm,gic-400") ||
        strstr(dev->compatible, "arm,cortex-a15-gic") ||
        strstr(dev->compatible, "arm,cortex-a9-gic")) {
        return PROBE_SCORE_EXACT;
    }
    
    // Check for GICv3
    if (strstr(dev->compatible, "arm,gic-v3")) {
        return PROBE_SCORE_EXACT;
    }
    
    return PROBE_SCORE_NONE;
}

static int gic_driver_attach(struct device *dev) {
    int ret = gic_probe(dev);
    if (ret != 0) {
        return ret;
    }
    
    // Enable the GIC after successful probe
    uart_puts("GIC: Enabling interrupt controller\n");
    gic_enable();
    
    // DEBUG: Verify distributor is actually enabled
    #ifdef __aarch64__
    if (gic_primary && gic_primary->version == GIC_V3 && gic_primary->dist_base) {
        uint32_t ctlr = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_CTLR);
        uart_puts("GIC: After enable, GICD_CTLR = ");
        uart_puthex(ctlr);
        if (ctlr & (1U << 31)) {
            uart_puts(" [RWP set!]");
        }
        if (!(ctlr & 0x3)) {
            uart_puts(" [WARNING: No enable bits set!]");
        }
        uart_puts("\n");
    }
    #endif
    
    return 0;
}

static int gic_driver_detach(struct device *dev) {
    (void)dev; // Not supported yet
    return -1;
}

static const struct driver_ops gic_driver_ops = {
    .probe = gic_driver_probe,
    .attach = gic_driver_attach,
    .detach = gic_driver_detach,
};

// Driver match table
static const struct device_match gic_matches[] = {
    { .type = MATCH_COMPATIBLE, .value = "arm,gic-400" },
    { .type = MATCH_COMPATIBLE, .value = "arm,cortex-a15-gic" },
    { .type = MATCH_COMPATIBLE, .value = "arm,cortex-a9-gic" },
    { .type = MATCH_COMPATIBLE, .value = "arm,gic-v2" },
    { .type = MATCH_COMPATIBLE, .value = "arm,gic-v3" },  // For future support
};

// GIC driver structure
static struct driver gic_driver = {
    .name = "arm-gic",
    .class = DRIVER_CLASS_INTC,
    .ops = &gic_driver_ops,
    .matches = gic_matches,
    .num_matches = sizeof(gic_matches) / sizeof(gic_matches[0]),
};

// Register the GIC driver
static void gic_driver_register(void) {
    driver_register(&gic_driver);
}

// Use the IRQCHIP driver module macro for automatic registration
IRQCHIP_DRIVER_MODULE(gic_driver_register, DRIVER_PRIO_EARLY);

#endif /* __aarch64__ */