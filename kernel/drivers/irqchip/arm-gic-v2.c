/*
 * kernel/drivers/irqchip/arm-gic-v2.c
 * 
 * ARM Generic Interrupt Controller v2 (GICv2) implementation
 */

#ifdef __aarch64__

#include <irqchip/arm-gic.h>
#include <uart.h>
#include <arch_io.h>
#include <stddef.h>

// GICv2-specific CPU interface register access
#define gic_cpu_read(gic, offset) \
    mmio_read32((uint8_t*)(gic)->cpu_base + (offset))
#define gic_cpu_write(gic, offset, val) \
    mmio_write32((uint8_t*)(gic)->cpu_base + (offset), (val))

// GICv2 distributor initialization
static void gicv2_dist_init(struct gic_data *gic) {
    uint32_t i;
    uint32_t typer;
    
    // Disable distributor
    gic_dist_write(gic, GICD_CTLR, 0);
    
    // Get the number of interrupts
    typer = gic_dist_read(gic, GICD_TYPER);
    gic->nr_irqs = ((typer & 0x1F) + 1) * 32;
    gic->nr_cpus = ((typer >> 5) & 0x7) + 1;
    
    uart_puts("GICv2: Detected ");
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

// GICv2 CPU interface initialization
static int gicv2_cpu_init(struct gic_data *gic) {
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
    
    return 0;
}

// GICv2 initialization
static int gicv2_init(struct gic_data *gic) {
    uart_puts("GICv2: Initializing\n");
    
    // Initialize distributor
    gicv2_dist_init(gic);
    
    // Initialize CPU interface
    gicv2_cpu_init(gic);
    
    uart_puts("GICv2: Initialization complete\n");
    return 0;
}

// GICv2 interrupt acknowledgment
static uint32_t gicv2_acknowledge_irq(struct gic_data *gic) {
    return gic_cpu_read(gic, GICC_IAR) & 0x3FF;
}

// GICv2 end of interrupt
static void gicv2_eoi(struct gic_data *gic, uint32_t hwirq) {
    gic_cpu_write(gic, GICC_EOIR, hwirq);
}

// GICv2 mask interrupt
static void gicv2_mask_irq(struct gic_data *gic, uint32_t hwirq) {
    uint32_t reg, bit;
    gic_irq_reg_pos(hwirq, &reg, &bit);
    gic_dist_write(gic, GICD_ICENABLER + reg, 1 << bit);
}

// GICv2 unmask interrupt
static void gicv2_unmask_irq(struct gic_data *gic, uint32_t hwirq) {
    uint32_t reg, bit;
    gic_irq_reg_pos(hwirq, &reg, &bit);
    gic_dist_write(gic, GICD_ISENABLER + reg, 1 << bit);
}

// GICv2 set interrupt priority
static void gicv2_set_priority(struct gic_data *gic, uint32_t hwirq, uint8_t priority) {
    uint32_t reg = hwirq / 4;
    uint32_t shift = (hwirq % 4) * 8;
    uint32_t val;
    
    val = gic_dist_read(gic, GICD_IPRIORITYR + (reg * 4));
    val &= ~(0xFF << shift);
    val |= (priority & 0xFF) << shift;
    gic_dist_write(gic, GICD_IPRIORITYR + (reg * 4), val);
}

// GICv2 set interrupt configuration
static void gicv2_set_config(struct gic_data *gic, uint32_t hwirq, uint32_t config) {
    uint32_t reg = hwirq / 16;
    uint32_t shift = (hwirq % 16) * 2;
    uint32_t val;
    
    if (hwirq < GIC_SPI_BASE) return;  // PPIs/SGIs have fixed config
    
    val = gic_dist_read(gic, GICD_ICFGR + (reg * 4));
    val &= ~(0x3 << shift);
    val |= (config & 0x3) << shift;
    gic_dist_write(gic, GICD_ICFGR + (reg * 4), val);
}

// GICv2 set interrupt target CPUs
static void gicv2_set_target(struct gic_data *gic, uint32_t hwirq, uint32_t target) {
    uint32_t reg = hwirq / 4;
    uint32_t shift = (hwirq % 4) * 8;
    uint32_t val;
    
    if (hwirq < GIC_SPI_BASE) return;  // PPIs/SGIs are per-CPU
    
    val = gic_dist_read(gic, GICD_ITARGETSR + (reg * 4));
    val &= ~(0xFF << shift);
    val |= (target & 0xFF) << shift;
    gic_dist_write(gic, GICD_ITARGETSR + (reg * 4), val);
}

// GICv2 send Software Generated Interrupt
static void gicv2_send_sgi(struct gic_data *gic, uint32_t sgi_id, uint32_t target) {
    if (sgi_id >= GIC_MAX_SGI) return;
    
    // Write to GICD_SGIR to generate the interrupt
    // Bits [25:24] = 0 (target list filter - use target_mask)
    // Bits [23:16] = target_mask (CPU targets)
    // Bits [3:0] = sgi_id (interrupt ID)
    uint32_t val = (target << 16) | sgi_id;
    gic_dist_write(gic, GICD_SGIR, val);
}

// GICv2 enable
static void gicv2_enable(struct gic_data *gic) {
    // Enable distributor
    gic_dist_write(gic, GICD_CTLR, 
                  GICD_CTLR_ENABLE_GRP0 | GICD_CTLR_ENABLE_GRP1);
    
    // Enable CPU interface
    gic_cpu_write(gic, GICC_CTLR,
                 GICC_CTLR_ENABLE_GRP0 | GICC_CTLR_ENABLE_GRP1);
}

// GICv2 disable
static void gicv2_disable(struct gic_data *gic) {
    // Disable CPU interface
    gic_cpu_write(gic, GICC_CTLR, 0);
    
    // Disable distributor
    gic_dist_write(gic, GICD_CTLR, 0);
}

// GICv2 operations structure
const struct gic_ops gicv2_ops = {
    .init = gicv2_init,
    .cpu_init = gicv2_cpu_init,
    .dist_init = gicv2_dist_init,
    .acknowledge_irq = gicv2_acknowledge_irq,
    .eoi = gicv2_eoi,
    .mask_irq = gicv2_mask_irq,
    .unmask_irq = gicv2_unmask_irq,
    .set_priority = gicv2_set_priority,
    .set_config = gicv2_set_config,
    .set_target = gicv2_set_target,
    .send_sgi = gicv2_send_sgi,
    .enable = gicv2_enable,
    .disable = gicv2_disable,
};

#endif /* __aarch64__ */