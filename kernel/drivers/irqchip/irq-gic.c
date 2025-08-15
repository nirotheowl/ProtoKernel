/*
 * kernel/drivers/irqchip/irq-gic.c
 * 
 * ARM Generic Interrupt Controller (GIC) driver implementation
 */

#include <irqchip/arm-gic.h>
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

// Global GIC instance
struct gic_data *gic_primary = NULL;

// Static GIC data for the primary controller
static struct gic_data primary_gic_data;

// Helper macros for register access
#define gic_dist_read(gic, offset) \
    mmio_read32((uint8_t*)(gic)->dist_base + (offset))
#define gic_dist_write(gic, offset, val) \
    mmio_write32((uint8_t*)(gic)->dist_base + (offset), (val))

#define gic_cpu_read(gic, offset) \
    mmio_read32((uint8_t*)(gic)->cpu_base + (offset))
#define gic_cpu_write(gic, offset, val) \
    mmio_write32((uint8_t*)(gic)->cpu_base + (offset), (val))

// Forward declarations for irq_chip operations
static void gic_irq_mask(struct irq_desc *desc);
static void gic_irq_unmask(struct irq_desc *desc);
static void gic_irq_eoi(struct irq_desc *desc);
static int gic_irq_set_type(struct irq_desc *desc, uint32_t type);

// IRQ chip operations for GIC
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

// Calculate register offset and bit position for an interrupt
static inline void gic_irq_reg_pos(uint32_t hwirq, uint32_t *reg, uint32_t *bit) {
    *reg = (hwirq / 32) * 4;
    *bit = hwirq % 32;
}

// Mask an interrupt
void gic_mask_irq(uint32_t hwirq) {
    struct gic_data *gic = gic_primary;
    uint32_t reg, bit;
    
    if (!gic) return;
    
    gic_irq_reg_pos(hwirq, &reg, &bit);
    gic_dist_write(gic, GICD_ICENABLER + reg, 1 << bit);
}

// Unmask an interrupt
void gic_unmask_irq(uint32_t hwirq) {
    struct gic_data *gic = gic_primary;
    uint32_t reg, bit;
    
    if (!gic) return;
    
    gic_irq_reg_pos(hwirq, &reg, &bit);
    gic_dist_write(gic, GICD_ISENABLER + reg, 1 << bit);
}

// End of interrupt
void gic_eoi(uint32_t hwirq) {
    struct gic_data *gic = gic_primary;
    if (!gic) return;
    
    gic_cpu_write(gic, GICC_EOIR, hwirq);
}

// Acknowledge interrupt
uint32_t gic_acknowledge_irq(void) {
    struct gic_data *gic = gic_primary;
    if (!gic) return GIC_SPURIOUS_IRQ;
    
    return gic_cpu_read(gic, GICC_IAR) & 0x3FF;
}

// Set interrupt priority
void gic_set_priority(uint32_t hwirq, uint8_t priority) {
    struct gic_data *gic = gic_primary;
    uint32_t reg = hwirq / 4;
    uint32_t shift = (hwirq % 4) * 8;
    uint32_t val;
    
    if (!gic) return;
    
    val = gic_dist_read(gic, GICD_IPRIORITYR + (reg * 4));
    val &= ~(0xFF << shift);
    val |= (priority & 0xFF) << shift;
    gic_dist_write(gic, GICD_IPRIORITYR + (reg * 4), val);
}

// Set interrupt target CPUs
void gic_set_target(uint32_t hwirq, uint8_t cpu_mask) {
    struct gic_data *gic = gic_primary;
    uint32_t reg = hwirq / 4;
    uint32_t shift = (hwirq % 4) * 8;
    uint32_t val;
    
    if (!gic || hwirq < GIC_SPI_BASE) return;  // PPIs/SGIs are per-CPU
    
    val = gic_dist_read(gic, GICD_ITARGETSR + (reg * 4));
    val &= ~(0xFF << shift);
    val |= (cpu_mask & 0xFF) << shift;
    gic_dist_write(gic, GICD_ITARGETSR + (reg * 4), val);
}

// Set interrupt configuration (edge/level)
void gic_set_config(uint32_t hwirq, uint32_t config) {
    struct gic_data *gic = gic_primary;
    uint32_t reg = hwirq / 16;
    uint32_t shift = (hwirq % 16) * 2;
    uint32_t val;
    
    if (!gic || hwirq < GIC_SPI_BASE) return;  // PPIs/SGIs have fixed config
    
    val = gic_dist_read(gic, GICD_ICFGR + (reg * 4));
    val &= ~(0x3 << shift);
    val |= (config & 0x3) << shift;
    gic_dist_write(gic, GICD_ICFGR + (reg * 4), val);
}

// IRQ chip operations implementation
static void gic_irq_mask(struct irq_desc *desc) {
    if (desc) {
        gic_mask_irq(desc->hwirq);
    }
}

static void gic_irq_unmask(struct irq_desc *desc) {
    if (desc) {
        gic_unmask_irq(desc->hwirq);
    }
}

static void gic_irq_eoi(struct irq_desc *desc) {
    if (desc) {
        gic_eoi(desc->hwirq);
    }
}

static int gic_irq_set_type(struct irq_desc *desc, uint32_t type) {
    if (!desc) return -1;
    
    uint32_t config = 0;
    if (type & IRQ_TYPE_EDGE_RISING) {
        config = 0x2;  // Edge-triggered
    }
    // Level-triggered is 0x0 (default)
    
    gic_set_config(desc->hwirq, config);
    return 0;
}

// Domain operations implementation
static int gic_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq) {
    (void)d; // Unused for now
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc) return -1;
    
    // Set the chip and handler
    desc->chip = &gic_chip;
    desc->chip_data = gic_primary;
    
    // Set default priority and target
    gic_set_priority(hwirq, GIC_PRIORITY_DEFAULT);
    gic_set_target(hwirq, 0x1);  // Target CPU0
    
    return 0;
}

static void gic_domain_unmap(struct irq_domain *d, uint32_t virq) {
    (void)d; // Unused for now
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc) return;
    
    // Mask the interrupt
    gic_mask_irq(desc->hwirq);
    
    // Clear chip data
    desc->chip = NULL;
    desc->chip_data = NULL;
}

static int gic_domain_xlate(struct irq_domain *d, const uint32_t *intspec,
                            uint32_t intsize, uint32_t *out_hwirq, uint32_t *out_type) {
    (void)d; // Unused for now
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

// Initialize GIC distributor
static void gic_dist_init(struct gic_data *gic) {
    uint32_t i;
    uint32_t typer;
    
    // Disable distributor
    gic_dist_write(gic, GICD_CTLR, 0);
    
    // Get the number of interrupts
    typer = gic_dist_read(gic, GICD_TYPER);
    gic->nr_irqs = ((typer & 0x1F) + 1) * 32;
    gic->nr_cpus = ((typer >> 5) & 0x7) + 1;
    
    uart_puts("GIC: Detected ");
    uart_putdec(gic->nr_irqs);
    uart_puts(" interrupts, ");
    uart_putdec(gic->nr_cpus);
    uart_puts(" CPUs\n");
    
    // Set all interrupts to group 0 (secure)
    for (i = 0; i < gic->nr_irqs; i += 32) {
        gic_dist_write(gic, GICD_IGROUPR + (i / 8), 0);
    }
    
    // Disable all interrupts
    for (i = 0; i < gic->nr_irqs; i += 32) {
        gic_dist_write(gic, GICD_ICENABLER + (i / 8), 0xFFFFFFFF);
    }
    
    // Clear all pending interrupts
    for (i = 0; i < gic->nr_irqs; i += 32) {
        gic_dist_write(gic, GICD_ICPENDR + (i / 8), 0xFFFFFFFF);
    }
    
    // Set default priority for all interrupts
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i += 4) {
        gic_dist_write(gic, GICD_IPRIORITYR + i, 
                      (GIC_PRIORITY_DEFAULT << 24) |
                      (GIC_PRIORITY_DEFAULT << 16) |
                      (GIC_PRIORITY_DEFAULT << 8) |
                      GIC_PRIORITY_DEFAULT);
    }
    
    // Set all SPIs to target CPU0
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i += 4) {
        gic_dist_write(gic, GICD_ITARGETSR + i, 0x01010101);
    }
    
    // Set all SPIs to level-triggered
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i += 16) {
        gic_dist_write(gic, GICD_ICFGR + (i / 4), 0);
    }
    
    // Enable distributor
    gic_dist_write(gic, GICD_CTLR, GICD_CTLR_ENABLE_GRP0);
}

// Initialize GIC CPU interface
static void gic_cpu_init(struct gic_data *gic) {
    uint32_t i;
    
    // Disable CPU interface
    gic_cpu_write(gic, GICC_CTLR, 0);
    
    // Set priority mask to allow all priorities
    gic_cpu_write(gic, GICC_PMR, 0xFF);
    
    // Set binary point to 0 (no preemption)
    gic_cpu_write(gic, GICC_BPR, 0);
    
    // Clear any pending interrupts
    for (i = 0; i < 32; i++) {
        uint32_t irq = gic_cpu_read(gic, GICC_IAR);
        if ((irq & 0x3FF) == GIC_SPURIOUS_IRQ)
            break;
        gic_cpu_write(gic, GICC_EOIR, irq);
    }
    
    // Enable CPU interface for group 0
    gic_cpu_write(gic, GICC_CTLR, GICC_CTLR_ENABLE_GRP0);
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
    
    gic->domain = domain;
    return domain;
}

// Enable GIC
void gic_enable(void) {
    if (!gic_primary) return;
    
    // Enable distributor
    gic_dist_write(gic_primary, GICD_CTLR, 
                  GICD_CTLR_ENABLE_GRP0 | GICD_CTLR_ENABLE_GRP1);
    
    // Enable CPU interface
    gic_cpu_write(gic_primary, GICC_CTLR,
                 GICC_CTLR_ENABLE_GRP0 | GICC_CTLR_ENABLE_GRP1);
}

// Disable GIC
void gic_disable(void) {
    if (!gic_primary) return;
    
    // Disable CPU interface
    gic_cpu_write(gic_primary, GICC_CTLR, 0);
    
    // Disable distributor
    gic_dist_write(gic_primary, GICD_CTLR, 0);
}

// Probe GIC from device tree
int gic_probe(struct device *dev) {
    struct gic_data *gic = &primary_gic_data;
    struct resource *res;
    
    uart_puts("GIC: Probing device\n");
    
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
    
    // Get CPU interface base address
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
    
    // Assume GICv2 for now
    gic->version = GIC_V2;
    gic->dev = dev;
    
    uart_puts("GIC: Distributor at 0x");
    uart_puthex((uint64_t)gic->dist_base);
    uart_puts(", CPU interface at 0x");
    uart_puthex((uint64_t)gic->cpu_base);
    uart_puts("\n");
    
    // Initialize GIC hardware
    gic_dist_init(gic);
    gic_cpu_init(gic);
    
    // Create IRQ domain
    if (!gic_create_domain(gic)) {
        uart_puts("GIC: Failed to create domain\n");
        return -1;
    }
    
    // Set as primary GIC
    gic_primary = gic;
    
    // Set the default domain
    irq_set_default_domain(gic->domain);
    
    uart_puts("GIC: Initialization complete\n");
    return 0;
}

// Initialize GIC (must be called after device tree probe)
int gic_init(void) {
    if (!gic_primary) {
        uart_puts("GIC: No GIC controller found - probe must be called first\n");
        return -1;
    }
    
    uart_puts("GIC: Enabling interrupt controller\n");
    gic_enable();
    
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
    
    // Check for GICv3 (not yet supported, but recognize it)
    if (strstr(dev->compatible, "arm,gic-v3")) {
        uart_puts("GIC: GICv3 detected but not yet supported\n");
        return PROBE_SCORE_NONE;
    }
    
    return PROBE_SCORE_NONE;
}

static int gic_driver_attach(struct device *dev) {
    return gic_probe(dev);
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
};

// GIC driver structure
static struct driver gic_driver = {
    .name = "gic",
    .class = DRIVER_CLASS_INTC,
    .ops = &gic_driver_ops,
    .matches = gic_matches,
    .num_matches = sizeof(gic_matches) / sizeof(gic_matches[0]),
    .priority = 0,  // Highest priority for interrupt controller
    .flags = DRIVER_FLAG_BUILTIN | DRIVER_FLAG_EARLY,
};

// Driver initialization function
static void gic_driver_init(void) {
    int ret;
    
    uart_puts("GIC: Registering driver\n");
    
    ret = driver_register(&gic_driver);
    if (ret == 0) {
        uart_puts("GIC: Driver registered successfully\n");
    } else {
        uart_puts("GIC: Failed to register driver\n");
    }
}

// Register as an early IRQCHIP driver
IRQCHIP_DRIVER_MODULE(gic_driver_init, DRIVER_PRIO_EARLY);