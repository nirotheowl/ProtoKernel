/*
 * kernel/include/irqchip/riscv-aplic.h
 * 
 * RISC-V Advanced Platform-Level Interrupt Controller (APLIC) definitions
 */

#ifndef _IRQCHIP_RISCV_APLIC_H
#define _IRQCHIP_RISCV_APLIC_H

#include <stdint.h>
#include <stdbool.h>
#include <irq/irq_domain.h>

// APLIC Constants
#define APLIC_MAX_SOURCE        1024
#define APLIC_MAX_IDC           16384   // Max Interrupt Delivery Controllers

// APLIC Register Offsets
#define APLIC_DOMAINCFG         0x0000
#define APLIC_SOURCECFG_BASE    0x0004
#define APLIC_MMSIADDRCFG       0x1bc0
#define APLIC_MMSIADDRCFGH      0x1bc4
#define APLIC_SMSIADDRCFG       0x1bc8
#define APLIC_SMSIADDRCFGH      0x1bcc
#define APLIC_SETIP_BASE        0x1c00
#define APLIC_SETIPNUM          0x1cdc
#define APLIC_CLRIP_BASE        0x1d00
#define APLIC_CLRIPNUM          0x1ddc
#define APLIC_SETIE_BASE        0x1e00
#define APLIC_SETIENUM          0x1edc
#define APLIC_CLRIE_BASE        0x1f00
#define APLIC_CLRIENUM          0x1fdc
#define APLIC_SETIPNUM_LE       0x2000
#define APLIC_SETIPNUM_BE       0x2004
#define APLIC_GENMSI            0x3000
#define APLIC_TARGET_BASE       0x3004

// IDC (Interrupt Delivery Controller) registers - per hart
#define APLIC_IDC_BASE          0x4000
#define APLIC_IDC_SIZE          32
#define APLIC_IDC_IDELIVERY     0x00
#define APLIC_IDC_IFORCE        0x04
#define APLIC_IDC_ITHRESHOLD    0x08
#define APLIC_IDC_TOPI          0x18
#define APLIC_IDC_CLAIMI        0x1c

// DOMAINCFG register bits
#define APLIC_DOMAINCFG_RDONLY  (1U << 31)
#define APLIC_DOMAINCFG_IE      (1U << 8)   // Interrupt Enable
#define APLIC_DOMAINCFG_DM      (1U << 2)   // Direct Mode
#define APLIC_DOMAINCFG_BE      (1U << 0)   // Big Endian

// SOURCECFG register bits and values
#define APLIC_SOURCECFG_D       (1U << 10)  // Delegate
#define APLIC_SOURCECFG_CHILDIDX_MASK  0x3ff
#define APLIC_SOURCECFG_SM_MASK         0x7
#define APLIC_SOURCECFG_SM_INACTIVE     0x0
#define APLIC_SOURCECFG_SM_DETACH       0x1
#define APLIC_SOURCECFG_SM_EDGE_RISE    0x4
#define APLIC_SOURCECFG_SM_EDGE_FALL    0x5
#define APLIC_SOURCECFG_SM_LEVEL_HIGH   0x6
#define APLIC_SOURCECFG_SM_LEVEL_LOW    0x7

// TARGET register bits for direct mode
#define APLIC_TARGET_HART_IDX_SHIFT     18
#define APLIC_TARGET_HART_IDX_MASK      0x3fff
#define APLIC_TARGET_GUEST_IDX_SHIFT    12
#define APLIC_TARGET_GUEST_IDX_MASK     0x3f
#define APLIC_TARGET_IPRIO_SHIFT        0
#define APLIC_TARGET_IPRIO_MASK         0xff

// TARGET register bits for MSI mode
#define APLIC_TARGET_EIID_SHIFT         0
#define APLIC_TARGET_EIID_MASK          0x7ff

// IDC TOPI register bits
#define APLIC_IDC_TOPI_ID_SHIFT         16
#define APLIC_IDC_TOPI_ID_MASK          0x3ff
#define APLIC_IDC_TOPI_PRIO_SHIFT       0
#define APLIC_IDC_TOPI_PRIO_MASK        0xff

// Default values
#define APLIC_DEFAULT_PRIORITY          1

// Forward declaration
struct imsic_data;

// APLIC MSI configuration
struct aplic_msicfg {
    uint64_t base_ppn;          // Base physical page number
    uint32_t hhxs;              // High hart index shift
    uint32_t hhxw;              // High hart index width
    uint32_t lhxs;              // Low hart index shift
    uint32_t lhxw;              // Low hart index width
};

// APLIC data structure
struct aplic_data {
    void *base;                 // MMIO base address
    uint32_t nr_sources;        // Number of interrupt sources
    uint32_t nr_harts;          // Number of harts
    uint32_t nr_idcs;           // Number of IDCs (direct mode)
    uint32_t hart_index_map[16]; // Map hart ID to IDC index (max 16 harts for now)
    struct irq_domain *domain;  // IRQ domain
    bool msi_mode;              // MSI mode enabled
    struct aplic_msicfg msicfg; // MSI configuration
    struct imsic_data *imsic;   // Link to IMSIC (if MSI mode)
};

// Global APLIC instance
extern struct aplic_data *aplic_primary;

// Common APLIC operations
int aplic_init(struct aplic_data *aplic);
void aplic_init_hw_global(struct aplic_data *aplic);

// Direct mode operations
int aplic_direct_init(struct aplic_data *aplic);
void aplic_direct_handle_irq(void);
uint32_t aplic_direct_claim(uint32_t idc);
void aplic_direct_complete(uint32_t idc, uint32_t irq);

// MSI mode operations
int aplic_msi_init(struct aplic_data *aplic);

// Register access helpers
static inline uint32_t aplic_read(struct aplic_data *aplic, uint32_t offset) {
    return *((volatile uint32_t *)((uint8_t *)aplic->base + offset));
}

static inline void aplic_write(struct aplic_data *aplic, uint32_t offset, uint32_t val) {
    *((volatile uint32_t *)((uint8_t *)aplic->base + offset)) = val;
}

// Source configuration helpers
static inline uint32_t aplic_sourcecfg_offset(uint32_t irq) {
    return APLIC_SOURCECFG_BASE + (irq - 1) * 4;
}

static inline uint32_t aplic_target_offset(uint32_t irq) {
    return APLIC_TARGET_BASE + (irq - 1) * 4;
}

// IDC register access helpers
static inline void *aplic_idc_base(struct aplic_data *aplic, uint32_t idc) {
    return (uint8_t *)aplic->base + APLIC_IDC_BASE + idc * APLIC_IDC_SIZE;
}

static inline uint32_t aplic_idc_read(struct aplic_data *aplic, uint32_t idc, uint32_t offset) {
    return *((volatile uint32_t *)((uint8_t *)aplic_idc_base(aplic, idc) + offset));
}

static inline void aplic_idc_write(struct aplic_data *aplic, uint32_t idc, uint32_t offset, uint32_t val) {
    *((volatile uint32_t *)((uint8_t *)aplic_idc_base(aplic, idc) + offset)) = val;
}

#endif /* _IRQCHIP_RISCV_APLIC_H */