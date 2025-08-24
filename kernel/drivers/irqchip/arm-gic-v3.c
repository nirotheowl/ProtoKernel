/*
 * kernel/drivers/irqchip/arm-gic-v3.c
 * 
 * ARM Generic Interrupt Controller v3 (GICv3) implementation
 */

#ifdef __aarch64__

#include <irqchip/arm-gic.h>
#include <arch_gic_sysreg.h>
#include <uart.h>
#include <arch_io.h>
#include <stddef.h>

// GICv3 Redistributor register access macros
#define gic_redist_read(addr, offset) \
    mmio_read32((uint8_t*)(addr) + (offset))
#define gic_redist_write(addr, offset, val) \
    mmio_write32((uint8_t*)(addr) + (offset), (val))

#define gic_redist_read64(addr, offset) \
    mmio_read64((uint8_t*)(addr) + (offset))
#define gic_redist_write64(addr, offset, val) \
    mmio_write64((uint8_t*)(addr) + (offset), (val))

// Per-CPU redistributor data
struct gicv3_redist_data {
    void *rd_base;          // RD_base (control registers)
    void *sgi_base;         // SGI_base (SGI/PPI configuration)
    uint64_t mpidr;         // MPIDR value for this CPU
    uint32_t processor_id;  // Processor ID from GICR_TYPER
};

// Maximum number of redistributors we'll support
#define MAX_REDIST 256
static struct gicv3_redist_data redistributors[MAX_REDIST];
static uint32_t nr_redistributors = 0;

// Wait for redistributor writes to complete
static void gicv3_redist_wait_for_rwp(void *rd_base) {
    uint32_t count = 1000000;
    
    while (gic_redist_read(rd_base, GICR_CTLR) & (1UL << 3)) {  // RWP bit (bit 3)
        if (--count == 0) {
            break;
        }
    }
}

// Wait for distributor writes to complete
static void gicv3_dist_wait_for_rwp(struct gic_data *gic) {
    uint32_t count = 100000;  // Reduced count to avoid long delays
    
    while (gic_dist_read(gic, GICD_CTLR) & GICD_CTLR_RWP) {
        if (--count == 0) {
            // Timeout - RWP bit stuck, continue anyway
            break;
        }
    }
}

// Discover and map redistributors
static int gicv3_redist_discover(struct gic_data *gic) {
    void *ptr = gic->redist_base;
    uint64_t typer;
    uint32_t aff;
    
    uart_puts("GICv3: Discovering redistributors\n");
    
    nr_redistributors = 0;
    
    // Walk through redistributor regions
    while (nr_redistributors < MAX_REDIST) {
        typer = gic_redist_read64(ptr, GICR_TYPER);
        aff = gic_redist_read(ptr, GICR_TYPER + 4);  // Affinity value
        
        redistributors[nr_redistributors].rd_base = ptr;
        redistributors[nr_redistributors].sgi_base = ptr + GICR_SGI_BASE;
        redistributors[nr_redistributors].mpidr = ((uint64_t)aff << 32) | 
                                                 (typer & 0xFFFFFF00);
        redistributors[nr_redistributors].processor_id = typer & 0xFFFF;
        
        uart_puts("GICv3: Found redistributor for CPU ");
        uart_putdec(nr_redistributors);
        uart_puts(" at ");
        uart_puthex((uint64_t)ptr);
        uart_puts("\n");
        
        nr_redistributors++;
        
        // Check if this is the last redistributor
        if (typer & (1ULL << 4)) {  // Last bit
            break;
        }
        
        // Move to next redistributor (each is 128KB: 64KB RD + 64KB SGI)
        ptr = (uint8_t*)ptr + 0x20000;
        
        // Check if we've gone beyond the mapped region
        if ((uint8_t*)ptr >= (uint8_t*)gic->redist_base + gic->redist_size) {
            break;
        }
    }
    
    uart_puts("GICv3: Found ");
    uart_putdec(nr_redistributors);
    uart_puts(" redistributors\n");
    
    gic->nr_cpus = nr_redistributors;
    
    return 0;
}

// Get redistributor for current CPU
static struct gicv3_redist_data* gicv3_get_current_redist(void) {
    uint64_t mpidr = read_mpidr();
    uint32_t aff = mpidr_to_affinity(mpidr);
    
    // TODO: Properly match MPIDR to redistributor
    // This will be needed for multi-core support:
    // for (uint32_t i = 0; i < nr_redistributors; i++) {
    //     if ((redistributors[i].mpidr & MPIDR_AFFINITY_MASK) == 
    //         (mpidr & MPIDR_AFFINITY_MASK)) {
    //         return &redistributors[i];
    //     }
    // }
    
    // For now, just use redistributor 0 (single CPU)
    if (nr_redistributors > 0) {
        return &redistributors[0];
    }
    
    return NULL;
}

// Initialize redistributor for current CPU
static int gicv3_redist_init(struct gic_data *gic) {
    struct gicv3_redist_data *rd = gicv3_get_current_redist();
    uint32_t waker;
    uint32_t i;
    
    if (!rd) {
        uart_puts("GICv3: No redistributor for current CPU\n");
        return -1;
    }
    
    uart_puts("GICv3: Initializing redistributor\n");
    
    // Wake up the redistributor
    waker = gic_redist_read(rd->rd_base, GICR_WAKER);
    waker &= ~GICR_WAKER_ProcessorSleep;
    gic_redist_write(rd->rd_base, GICR_WAKER, waker);
    
    // Wait for children to wake up
    while (gic_redist_read(rd->rd_base, GICR_WAKER) & GICR_WAKER_ChildrenAsleep) {
        __asm__ volatile("nop");
    }
    
    // Configure SGIs and PPIs
    
    // Set all SGIs/PPIs to group 1
    gic_redist_write(rd->sgi_base, GICD_IGROUPR, 0xFFFFFFFF);
    
    // Disable all SGIs/PPIs
    gic_redist_write(rd->sgi_base, GICD_ICENABLER, 0xFFFFFFFF);
    
    // Clear all pending SGIs/PPIs
    gic_redist_write(rd->sgi_base, GICD_ICPENDR, 0xFFFFFFFF);
    
    // Set default priority for SGIs/PPIs
    for (i = 0; i < 32; i += 4) {
        gic_redist_write(rd->sgi_base, GICD_IPRIORITYR + i,
                        (GIC_PRIORITY_DEFAULT << 24) |
                        (GIC_PRIORITY_DEFAULT << 16) |
                        (GIC_PRIORITY_DEFAULT << 8) |
                        GIC_PRIORITY_DEFAULT);
    }
    
    // Wait for writes to complete
    gicv3_redist_wait_for_rwp(rd->rd_base);
    
    return 0;
}

// GICv3 distributor initialization
static void gicv3_dist_init(struct gic_data *gic) {
    uint32_t i;
    uint32_t typer;
    uint64_t affinity;
    uint32_t ctlr;
    
    // Read current control register to check security state
    ctlr = gic_dist_read(gic, GICD_CTLR);
    
    // Check if we're in a single security state (DS bit set)
    // If DS is not set, we may need to set it for non-secure operation
    if (!(ctlr & GICD_CTLR_DS)) {
        uart_puts("GICv3: Security enabled, setting DS bit for non-secure operation\n");
        gic_dist_write(gic, GICD_CTLR, ctlr | GICD_CTLR_DS);
        gicv3_dist_wait_for_rwp(gic);
        
        // Verify DS bit was set
        ctlr = gic_dist_read(gic, GICD_CTLR);
        if (!(ctlr & GICD_CTLR_DS)) {
            uart_puts("GICv3: WARNING: Could not disable security (DS bit not set)\n");
        }
    }
    
    // Disable distributor
    gic_dist_write(gic, GICD_CTLR, ctlr & ~0x3);
    gicv3_dist_wait_for_rwp(gic);
    
    // Get the number of interrupts
    typer = gic_dist_read(gic, GICD_TYPER);
    gic->nr_irqs = ((typer & 0x1F) + 1) * 32;
    
    uart_puts("GICv3: Detected ");
    uart_putdec(gic->nr_irqs);
    uart_puts(" interrupts\n");
    
    // Get default affinity routing (route to CPU 0)
    affinity = read_mpidr() & MPIDR_AFFINITY_MASK;
    
    // Set all SPIs to group 1
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i += 32) {
        gic_dist_write(gic, GICD_IGROUPR + (i / 8), 0xFFFFFFFF);
    }
    
    // Disable all SPIs
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i += 32) {
        gic_dist_write(gic, GICD_ICENABLER + (i / 8), 0xFFFFFFFF);
    }
    
    // Clear all pending SPIs
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i += 32) {
        gic_dist_write(gic, GICD_ICPENDR + (i / 8), 0xFFFFFFFF);
    }
    
    // Set default priority for all SPIs
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i += 4) {
        gic_dist_write(gic, GICD_IPRIORITYR + i,
                      (GIC_PRIORITY_DEFAULT << 24) |
                      (GIC_PRIORITY_DEFAULT << 16) |
                      (GIC_PRIORITY_DEFAULT << 8) |
                      GIC_PRIORITY_DEFAULT);
    }
    
    // Set all SPIs to default affinity (CPU 0)
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i++) {
        gic_dist_write64(gic, GICD_IROUTER + (i * 8), affinity);
    }
    
    // Set all SPIs to level-triggered
    for (i = GIC_SPI_BASE; i < gic->nr_irqs; i += 16) {
        gic_dist_write(gic, GICD_ICFGR + (i / 4), 0);
    }
    
    // Wait for writes to complete
    gicv3_dist_wait_for_rwp(gic);
    
    // Enable distributor with ARE, Group 1, and Group 1A
    // Read current value to preserve DS bit and other settings
    ctlr = gic_dist_read(gic, GICD_CTLR);
    ctlr |= GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A | GICD_CTLR_ENABLE_G1;
    gic_dist_write(gic, GICD_CTLR, ctlr);
    
    // Wait for the enable to complete
    gicv3_dist_wait_for_rwp(gic);
}

// GICv3 CPU interface initialization (using system registers)
static int gicv3_cpu_init(struct gic_data *gic) {
    uint32_t sre;
    
    uart_puts("GICv3: Initializing CPU interface\n");
    
    // Enable system register access
    sre = gicv3_read_sre();
    if (!(sre & ICC_SRE_EL1_SRE)) {
        // Try to enable if not already enabled
        gicv3_write_sre(sre | ICC_SRE_EL1_SRE);
        sre = gicv3_read_sre();
        if (!(sre & ICC_SRE_EL1_SRE)) {
            uart_puts("GICv3: Failed to enable system register access\n");
            return -1;
        }
    }
    
    // Ensure bypass is disabled
    gicv3_write_sre(sre | ICC_SRE_EL1_DFB | ICC_SRE_EL1_DIB);
    
    // Set priority mask to allow all priorities
    gicv3_write_pmr(0xFF);
    
    // Set binary point register
    gicv3_write_bpr1(0);
    
    // Enable EOI deactivation split (for proper interrupt handling)
    gicv3_write_ctlr(0);  // Clear EOImode bit for now
    
    // Enable Group 1 interrupts
    gicv3_write_grpen1(ICC_IGRPEN1_EL1_Enable);
    
    // Ensure changes are visible
    __asm__ volatile("isb");
    
    return 0;
}

// GICv3 initialization
static int gicv3_init(struct gic_data *gic) {
    uart_puts("GICv3: Initializing\n");
    
    // Discover redistributors
    if (gicv3_redist_discover(gic) != 0) {
        uart_puts("GICv3: Failed to discover redistributors\n");
        return -1;
    }
    
    // Initialize distributor
    gicv3_dist_init(gic);
    
    // Initialize redistributor for current CPU
    if (gicv3_redist_init(gic) != 0) {
        uart_puts("GICv3: Failed to initialize redistributor\n");
        return -1;
    }
    
    // Initialize CPU interface
    if (gicv3_cpu_init(gic) != 0) {
        uart_puts("GICv3: Failed to initialize CPU interface\n");
        return -1;
    }
    
    uart_puts("GICv3: Initialization complete\n");
    return 0;
}

// GICv3 interrupt acknowledgment
static uint32_t gicv3_acknowledge_irq(struct gic_data *gic) {
    uint32_t irq = gicv3_read_iar1();
    return irq & 0xFFFFFF;  // 24-bit IRQ number
}

// GICv3 end of interrupt
static void gicv3_eoi(struct gic_data *gic, uint32_t hwirq) {
    gicv3_write_eoir1(hwirq);
}

// GICv3 mask interrupt
static void gicv3_mask_irq(struct gic_data *gic, uint32_t hwirq) {
    uint32_t reg, bit;
    
    if (hwirq < GIC_SPI_BASE) {
        // SGI/PPI - use redistributor
        struct gicv3_redist_data *rd = gicv3_get_current_redist();
        if (rd) {
            gic_irq_reg_pos(hwirq, &reg, &bit);
            gic_redist_write(rd->sgi_base, GICD_ICENABLER + reg, 1 << bit);
            // Wait for RWP after disable operations
            gicv3_redist_wait_for_rwp(rd->rd_base);
        }
    } else {
        // SPI - use distributor
        gic_irq_reg_pos(hwirq, &reg, &bit);
        gic_dist_write(gic, GICD_ICENABLER + reg, 1 << bit);
        // Note: Linux waits for RWP here, but we skip it to avoid potential issues
        // with RWP bit getting stuck. The write should complete eventually.
        // gicv3_dist_wait_for_rwp(gic);
    }
}

// GICv3 unmask interrupt
static void gicv3_unmask_irq(struct gic_data *gic, uint32_t hwirq) {
    uint32_t reg, bit;
    
    if (hwirq < GIC_SPI_BASE) {
        // SGI/PPI - use redistributor
        struct gicv3_redist_data *rd = gicv3_get_current_redist();
        if (rd) {
            gic_irq_reg_pos(hwirq, &reg, &bit);
            gic_redist_write(rd->sgi_base, GICD_ISENABLER + reg, 1 << bit);
        }
    } else {
        // SPI - use distributor
        gic_irq_reg_pos(hwirq, &reg, &bit);
        gic_dist_write(gic, GICD_ISENABLER + reg, 1 << bit);
    }
}

// GICv3 set interrupt priority
static void gicv3_set_priority(struct gic_data *gic, uint32_t hwirq, uint8_t priority) {
    uint32_t reg = hwirq / 4;
    uint32_t shift = (hwirq % 4) * 8;
    uint32_t val;
    
    if (hwirq < GIC_SPI_BASE) {
        // SGI/PPI - use redistributor
        struct gicv3_redist_data *rd = gicv3_get_current_redist();
        if (rd) {
            val = gic_redist_read(rd->sgi_base, GICD_IPRIORITYR + (reg * 4));
            val &= ~(0xFF << shift);
            val |= (priority & 0xFF) << shift;
            gic_redist_write(rd->sgi_base, GICD_IPRIORITYR + (reg * 4), val);
        }
    } else {
        // SPI - use distributor
        val = gic_dist_read(gic, GICD_IPRIORITYR + (reg * 4));
        val &= ~(0xFF << shift);
        val |= (priority & 0xFF) << shift;
        gic_dist_write(gic, GICD_IPRIORITYR + (reg * 4), val);
    }
}

// GICv3 set interrupt configuration
static void gicv3_set_config(struct gic_data *gic, uint32_t hwirq, uint32_t config) {
    uint32_t reg = hwirq / 16;
    uint32_t shift = (hwirq % 16) * 2;
    uint32_t val;
    
    if (hwirq < GIC_PPI_BASE) return;  // SGIs have fixed config
    
    if (hwirq < GIC_SPI_BASE) {
        // PPI - use redistributor
        struct gicv3_redist_data *rd = gicv3_get_current_redist();
        if (rd) {
            val = gic_redist_read(rd->sgi_base, GICD_ICFGR + (reg * 4));
            val &= ~(0x3 << shift);
            val |= (config & 0x3) << shift;
            gic_redist_write(rd->sgi_base, GICD_ICFGR + (reg * 4), val);
        }
    } else {
        // SPI - use distributor
        val = gic_dist_read(gic, GICD_ICFGR + (reg * 4));
        val &= ~(0x3 << shift);
        val |= (config & 0x3) << shift;
        gic_dist_write(gic, GICD_ICFGR + (reg * 4), val);
    }
}

// GICv3 set interrupt target (affinity routing)
static void gicv3_set_target(struct gic_data *gic, uint32_t hwirq, uint32_t target) {
    uint64_t affinity;
    
    if (hwirq < GIC_SPI_BASE) return;  // SGIs/PPIs are local
    
    // For now, interpret target as CPU index 0-7
    // In real implementation, this would map to actual affinity values
    if (target == 0 || target == 1) {
        // Route to CPU 0 (current CPU)
        affinity = read_mpidr() & MPIDR_AFFINITY_MASK;
    } else {
        // For other CPUs, would need proper affinity mapping
        affinity = read_mpidr() & MPIDR_AFFINITY_MASK;
    }
    
    // Set interrupt routing
    gic_dist_write64(gic, GICD_IROUTER + (hwirq * 8), affinity);
}

// GICv3 send Software Generated Interrupt
static void gicv3_send_sgi(struct gic_data *gic, uint32_t sgi_id, uint32_t target) {
    uint64_t val = 0;
    
    if (sgi_id >= GIC_MAX_SGI) return;
    
    // Build SGI register value
    // For now, simple implementation targeting specific CPUs
    if (target == 0xFF) {
        // Broadcast to all CPUs except self
        val = ICC_SGI1R_IRM | (sgi_id << ICC_SGI1R_INTID_SHIFT);
    } else {
        // Target specific CPUs (simplified - assumes target is CPU 0)
        val = (1ULL << ICC_SGI1R_TARGET_LIST_SHIFT) |  // Target list
              (sgi_id << ICC_SGI1R_INTID_SHIFT);        // Interrupt ID
        // Affinity values would be set based on target CPU MPIDR
    }
    
    // Send the SGI
    gicv3_write_sgi1r(val);
    
    // Ensure SGI is sent
    __asm__ volatile("isb");
}

// GICv3 enable
static void gicv3_enable(struct gic_data *gic) {
    uint32_t val;
    
    // Read current value to preserve any implementation-specific bits
    val = gic_dist_read(gic, GICD_CTLR);
    
    // Clear RWP bit if it's stuck (should not be writable)
    val &= ~GICD_CTLR_RWP;
    
    // Ensure ARE, Group 1, and Group 1A are enabled
    val |= GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A | GICD_CTLR_ENABLE_G1;
    
    gic_dist_write(gic, GICD_CTLR, val);
    
    // Wait for write to complete
    gicv3_dist_wait_for_rwp(gic);
    
    // Enable Group 1 interrupts at CPU interface
    gicv3_write_grpen1(ICC_IGRPEN1_EL1_Enable);
    
    // Ensure changes are visible
    __asm__ volatile("isb");  // ISB is ARM-specific, OK for system register sync
}

// GICv3 disable
static void gicv3_disable(struct gic_data *gic) {
    uint32_t ctlr;
    
    // Disable Group 1 interrupts at CPU interface
    gicv3_write_grpen1(0);
    
    // Disable distributor but preserve DS bit and ARE
    ctlr = gic_dist_read(gic, GICD_CTLR);
    ctlr &= ~(GICD_CTLR_ENABLE_G1 | GICD_CTLR_ENABLE_G1A);
    gic_dist_write(gic, GICD_CTLR, ctlr);
    
    // Wait for write to complete
    gicv3_dist_wait_for_rwp(gic);
    
    // Ensure changes are visible
    __asm__ volatile("isb");
}

// GICv3 operations structure
const struct gic_ops gicv3_ops = {
    .init = gicv3_init,
    .cpu_init = gicv3_cpu_init,
    .dist_init = gicv3_dist_init,
    .acknowledge_irq = gicv3_acknowledge_irq,
    .eoi = gicv3_eoi,
    .mask_irq = gicv3_mask_irq,
    .unmask_irq = gicv3_unmask_irq,
    .set_priority = gicv3_set_priority,
    .set_config = gicv3_set_config,
    .set_target = gicv3_set_target,
    .send_sgi = gicv3_send_sgi,
    .enable = gicv3_enable,
    .disable = gicv3_disable,
};

#endif /* __aarch64__ */