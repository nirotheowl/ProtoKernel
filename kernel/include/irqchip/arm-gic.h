/*
 * kernel/include/irqchip/arm-gic.h
 * 
 * ARM Generic Interrupt Controller (GIC) driver interface
 */

#ifndef _IRQCHIP_ARM_GIC_H_
#define _IRQCHIP_ARM_GIC_H_

#include <stdint.h>
#include <irq/irq_domain.h>
#include <device/device.h>

// GIC versions
enum gic_version {
    GIC_V2,
    GIC_V3,
};

// GIC Distributor registers (GICD)
#define GICD_CTLR       0x0000  // Distributor Control Register
#define GICD_TYPER      0x0004  // Interrupt Controller Type Register
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
#define GICD_SGIR       0x0F00  // Software Generated Interrupt Register (GICv2 only)

// GIC CPU Interface registers (GICC) - GICv2
#define GICC_CTLR       0x0000  // CPU Interface Control Register
#define GICC_PMR        0x0004  // Interrupt Priority Mask Register
#define GICC_BPR        0x0008  // Binary Point Register
#define GICC_IAR        0x000C  // Interrupt Acknowledge Register
#define GICC_EOIR       0x0010  // End of Interrupt Register
#define GICC_RPR        0x0014  // Running Priority Register
#define GICC_HPPIR      0x0018  // Highest Priority Pending Interrupt Register
#define GICC_ABPR       0x001C  // Aliased Binary Point Register
#define GICC_AIAR       0x0020  // Aliased Interrupt Acknowledge Register
#define GICC_AEOIR      0x0024  // Aliased End of Interrupt Register
#define GICC_AHPPIR     0x0028  // Aliased Highest Priority Pending Interrupt Register

// Control register bits
#define GICD_CTLR_ENABLE_GRP0   (1 << 0)
#define GICD_CTLR_ENABLE_GRP1   (1 << 1)

#define GICC_CTLR_ENABLE_GRP0   (1 << 0)
#define GICC_CTLR_ENABLE_GRP1   (1 << 1)
#define GICC_CTLR_EOI_MODE      (1 << 9)

// Interrupt IDs
#define GIC_SGI_BASE            0       // Software Generated Interrupts (0-15)
#define GIC_PPI_BASE            16      // Private Peripheral Interrupts (16-31)
#define GIC_SPI_BASE            32      // Shared Peripheral Interrupts (32+)

#define GIC_MAX_SGI             16
#define GIC_MAX_PPI             16
#define GIC_MAX_INTERRUPTS      1020    // Maximum supported by GICv2

// Priority levels
#define GIC_PRIORITY_DEFAULT    0xA0
#define GIC_PRIORITY_MASK       0xFF

// Special interrupt IDs
#define GIC_SPURIOUS_IRQ        1023

// GIC driver data structure
struct gic_data {
    void *dist_base;            // Distributor base address
    void *cpu_base;             // CPU interface base address (GICv2)
    enum gic_version version;   // GIC version
    uint32_t nr_irqs;           // Number of interrupts
    uint32_t nr_cpus;           // Number of CPUs
    struct irq_domain *domain;  // IRQ domain for this GIC
    struct device *dev;         // Associated device
};

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

// GIC domain operations
struct irq_domain *gic_create_domain(struct gic_data *gic);

// Software Generated Interrupt
void gic_send_sgi(uint32_t sgi_id, uint32_t target_mask);

// Global GIC instance (for now, single GIC support)
extern struct gic_data *gic_primary;

#endif /* _IRQCHIP_ARM_GIC_H_ */