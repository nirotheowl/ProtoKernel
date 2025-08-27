/*
 * kernel/include/irqchip/arm-gic.h
 * 
 * ARM Generic Interrupt Controller (GIC) driver interface
 */

#ifndef _IRQCHIP_ARM_GIC_H_
#define _IRQCHIP_ARM_GIC_H_

#ifdef __aarch64__

#include <stdint.h>
#include <irq/irq_domain.h>
#include <device/device.h>
#include <arch_io.h>

// GIC versions
enum gic_version {
    GIC_V2,
    GIC_V3,
    GIC_V4,  // Future extension
};

// Forward declaration
struct gic_data;

// GIC operations - version-specific implementations
struct gic_ops {
    // Initialization
    int (*init)(struct gic_data *gic);
    int (*cpu_init)(struct gic_data *gic);
    void (*dist_init)(struct gic_data *gic);
    
    // Interrupt acknowledgment and EOI
    uint32_t (*acknowledge_irq)(struct gic_data *gic);
    void (*eoi)(struct gic_data *gic, uint32_t hwirq);
    
    // Interrupt control
    void (*mask_irq)(struct gic_data *gic, uint32_t hwirq);
    void (*unmask_irq)(struct gic_data *gic, uint32_t hwirq);
    void (*set_priority)(struct gic_data *gic, uint32_t hwirq, uint8_t priority);
    void (*set_config)(struct gic_data *gic, uint32_t hwirq, uint32_t config);
    
    // CPU targeting (v2) / Affinity routing (v3)
    void (*set_target)(struct gic_data *gic, uint32_t hwirq, uint32_t target);
    
    // Software Generated Interrupts
    void (*send_sgi)(struct gic_data *gic, uint32_t sgi_id, uint32_t target);
    
    // Enable/disable
    void (*enable)(struct gic_data *gic);
    void (*disable)(struct gic_data *gic);
    
    // MSI support
    int (*msi_init)(struct gic_data *gic);
    void (*msi_compose_msg)(struct gic_data *gic, uint32_t hwirq, 
                           uint32_t *addr_hi, uint32_t *addr_lo, uint32_t *data);
};

// GIC data structure
struct gic_data {
    // Version and operations
    enum gic_version version;
    const struct gic_ops *ops;
    
    // Memory mapped regions
    void *dist_base;            // Distributor base address
    void *cpu_base;             // CPU interface base (GICv2 only)
    void *redist_base;          // Redistributor base (GICv3+ only)
    size_t redist_size;         // Size of redistributor region
    
    // Configuration
    uint32_t nr_irqs;           // Number of interrupts
    uint32_t nr_cpus;           // Number of CPUs
    
    // IRQ domain
    struct irq_domain *domain;  // IRQ domain for this GIC
    
    // Device reference
    struct device *dev;         // Associated device
    
    // Future extension fields for LPI/ITS (GICv3+)
    void *its_base;             // ITS base address (unused initially)
    uint64_t *lpi_config_table; // LPI configuration (unused initially)
    uint64_t *lpi_pending_table;// LPI pending bits (unused initially)
    uint32_t nr_lpis;           // Number of LPIs (unused initially)
    
    // MSI support
    uint64_t msi_doorbell_addr;     // Physical address for MSI doorbell
    uint32_t msi_spi_base;          // Base SPI for MSI allocation
    uint32_t msi_spi_count;         // Number of SPIs available for MSI
    void *msi_doorbell_virt;        // Virtual mapping of doorbell region
    uint32_t msi_typer;             // GICv2m MSI_TYPER register value
    uint32_t msi_flags;             // MSI implementation flags
    
    // MSI SPI allocation tracking
    uint32_t *msi_bitmap;           // Bitmap for MSI SPI allocation
    uint32_t msi_bitmap_size;       // Size of bitmap in words
};

// Common GIC distributor registers (same offset for v2/v3)
#define GICD_CTLR       0x0000  // Distributor Control Register
#define GICD_TYPER      0x0004  // Interrupt Controller Type Register
#define GICD_TYPER_MBIS (1U << 16)  // Message Based Interrupt Support (GICv3)
#define GICD_IIDR       0x0008  // Distributor Implementer Identification Register
#define GICD_IGROUPR    0x0080  // Interrupt Group Registers
#define GICD_ISENABLER  0x0100  // Interrupt Set-Enable Registers
#define GICD_ICENABLER  0x0180  // Interrupt Clear-Enable Registers
#define GICD_ISPENDR    0x0200  // Interrupt Set-Pending Registers
#define GICD_ICPENDR    0x0280  // Interrupt Clear-Pending Registers
#define GICD_ISACTIVER  0x0300  // Interrupt Set-Active Registers
#define GICD_ICACTIVER  0x0380  // Interrupt Clear-Active Registers
#define GICD_IPRIORITYR 0x0400  // Interrupt Priority Registers
#define GICD_ITARGETSR  0x0800  // Interrupt Processor Targets Registers (GICv2 only)
#define GICD_ICFGR      0x0C00  // Interrupt Configuration Registers
#define GICD_IGRPMODR   0x0D00  // Interrupt Group Modifier Registers (GICv3)
#define GICD_NSACR      0x0E00  // Non-secure Access Control Registers
#define GICD_SGIR       0x0F00  // Software Generated Interrupt Register (GICv2 only)
#define GICD_CPENDSGIR  0x0F10  // Clear Pending Software Generated Interrupts
#define GICD_SPENDSGIR  0x0F20  // Set Pending Software Generated Interrupts
#define GICD_IROUTER    0x6000  // Interrupt Routing Registers (GICv3+ only)

// GIC CPU Interface registers (GICv2 only)
#define GICC_CTLR       0x0000  // CPU Interface Control Register
#define GICC_PMR        0x0004  // Interrupt Priority Mask Register
#define GICC_BPR        0x0008  // Binary Point Register
#define GICC_IAR        0x000C  // Interrupt Acknowledge Register
#define GICC_EOIR       0x0010  // End of Interrupt Register
#define GICC_RPR        0x0014  // Running Priority Register
#define GICC_HPPIR      0x0018  // Highest Priority Pending Interrupt Register

// GIC Redistributor registers (GICv3+ only)
#define GICR_CTLR       0x0000  // Redistributor Control Register
#define GICR_TYPER      0x0008  // Redistributor Type Register
#define GICR_WAKER      0x0014  // Redistributor Wake Register
#define GICR_PROPBASER  0x0070  // LPI Configuration Table Base (future)
#define GICR_PENDBASER  0x0078  // LPI Pending Table Base (future)

// SGI/PPI registers offset from redistributor base (GICv3+)
#define GICR_SGI_BASE   0x10000 // Offset to SGI/PPI registers

// Control register bits
#define GICD_CTLR_ENABLE_GRP0   (1 << 0)  // GICv2: Group 0, GICv3: Group 1 (Secure)
#define GICD_CTLR_ENABLE_GRP1   (1 << 1)  // GICv2: Group 1, GICv3: Group 1A (Non-secure)
#define GICD_CTLR_ARE_NS        (1 << 4)  // GICv3: Enable affinity routing
#define GICD_CTLR_DS            (1 << 6)  // GICv3: Disable Security
#define GICD_CTLR_RWP           (1U << 31) // GICv3: Register Write Pending
// For clarity in GICv3 code:
#define GICD_CTLR_ENABLE_G1     GICD_CTLR_ENABLE_GRP0
#define GICD_CTLR_ENABLE_G1A    GICD_CTLR_ENABLE_GRP1

#define GICC_CTLR_ENABLE_GRP0   (1 << 0)
#define GICC_CTLR_ENABLE_GRP1   (1 << 1)

// Redistributor bits (GICv3+)
#define GICR_WAKER_ProcessorSleep    (1 << 1)
#define GICR_WAKER_ChildrenAsleep    (1 << 2)

// Interrupt IDs
#define GIC_SGI_BASE            0       // Software Generated Interrupts (0-15)
#define GIC_PPI_BASE            16      // Private Peripheral Interrupts (16-31)
#define GIC_SPI_BASE            32      // Shared Peripheral Interrupts (32+)

#define GIC_MAX_SGI             16
#define GIC_MAX_PPI             16
#define GIC_MAX_INTERRUPTS      1020    // Maximum SPIs in GICv2/v3
#define GIC_LPI_BASE            8192    // LPIs start here (GICv3+, future)

// Priority levels
#define GIC_PRIORITY_DEFAULT    0xA0
#define GIC_PRIORITY_MASK       0xFF

// Special interrupt IDs
#define GIC_SPURIOUS_IRQ        1023

// MSI/MBI registers (GICv3 only, but defined here for consistency)
#define GICD_SETSPI_NSR         0x0040  // Set SPI Non-secure Register
#define GICD_CLRSPI_NSR         0x0048  // Clear SPI Non-secure Register
#define GICD_SETSPI_SR          0x0050  // Set SPI Secure Register
#define GICD_CLRSPI_SR          0x0058  // Clear SPI Secure Register

// GICv2m MSI registers (when available)
#define V2M_MSI_TYPER           0x0008  // MSI Type Register
#define V2M_MSI_SETSPI_NS       0x0040  // MSI Set SPI Non-secure
#define V2M_MSI_IIDR            0x0FCC  // MSI Interface ID Register

// MSI capability flags
#define GIC_MSI_FLAGS_NONE      0x00000000
#define GIC_MSI_FLAGS_V2M       0x00000001  // GICv2m hardware present
#define GIC_MSI_FLAGS_MBI       0x00000002  // GICv3 MBI support
#define GIC_MSI_FLAGS_ITS       0x00000004  // GICv3 ITS present (future)
#define GIC_MSI_FLAGS_EMULATED  0x00000008  // Using emulated MSI

// V2M MSI_TYPER field extraction
#define V2M_MSI_TYPER_BASE_SHIFT    16
#define V2M_MSI_TYPER_BASE_MASK     0x3FF
#define V2M_MSI_TYPER_NUM_MASK      0x3FF
#define V2M_MSI_TYPER_BASE_SPI(x)   (((x) >> V2M_MSI_TYPER_BASE_SHIFT) & V2M_MSI_TYPER_BASE_MASK)
#define V2M_MSI_TYPER_NUM_SPI(x)    ((x) & V2M_MSI_TYPER_NUM_MASK)

// Helper macros for register access
#define gic_dist_read(gic, offset) \
    mmio_read32((uint8_t*)(gic)->dist_base + (offset))
#define gic_dist_write(gic, offset, val) \
    mmio_write32((uint8_t*)(gic)->dist_base + (offset), (val))
#define gic_dist_read64(gic, offset) \
    mmio_read64((uint8_t*)(gic)->dist_base + (offset))
#define gic_dist_write64(gic, offset, val) \
    mmio_write64((uint8_t*)(gic)->dist_base + (offset), (val))

// Calculate register offset and bit position for an interrupt
static inline void gic_irq_reg_pos(uint32_t hwirq, uint32_t *reg, uint32_t *bit) {
    *reg = (hwirq / 32) * 4;
    *bit = hwirq % 32;
}

// Version-specific ops (defined in arm-gic-v2.c and arm-gic-v3.c)
extern const struct gic_ops gicv2_ops;
extern const struct gic_ops gicv3_ops;

// GIC initialization and management
int gic_init(void);
int gic_probe(struct device *dev);
void gic_enable(void);
void gic_disable(void);

// Interrupt operations
void gic_mask_irq(uint32_t hwirq);
void gic_unmask_irq(uint32_t hwirq);
void gic_eoi(uint32_t hwirq);
uint32_t gic_acknowledge_irq(void);
void gic_set_priority(uint32_t hwirq, uint8_t priority);
void gic_set_target(uint32_t hwirq, uint8_t cpu_mask);
void gic_set_config(uint32_t hwirq, uint32_t config);

// Software Generated Interrupt
void gic_send_sgi(uint32_t sgi_id, uint32_t target_mask);

// Global GIC instance (for now, single GIC support)
extern struct gic_data *gic_primary;

#endif /* __aarch64__ */

#endif /* _IRQCHIP_ARM_GIC_H_ */