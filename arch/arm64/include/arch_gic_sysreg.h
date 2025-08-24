/*
 * arch/arm64/include/arch_gic_sysreg.h
 * 
 * ARM64 GICv3 System Register Definitions
 */

#ifndef _ARCH_GIC_SYSREG_H_
#define _ARCH_GIC_SYSREG_H_

#ifdef __aarch64__

#include <stdint.h>

// GICv3 ICC System Register Encodings
// Format: S<op0>_<op1>_C<CRn>_C<CRm>_<op2>
#define ICC_PMR_EL1         "S3_0_C4_C6_0"     // Priority Mask Register
#define ICC_IAR0_EL1        "S3_0_C12_C8_0"    // Interrupt Acknowledge Register (Group 0)
#define ICC_IAR1_EL1        "S3_0_C12_C12_0"   // Interrupt Acknowledge Register (Group 1)
#define ICC_EOIR0_EL1       "S3_0_C12_C8_1"    // End of Interrupt Register (Group 0)
#define ICC_EOIR1_EL1       "S3_0_C12_C12_1"   // End of Interrupt Register (Group 1)
#define ICC_HPPIR0_EL1      "S3_0_C12_C8_2"    // Highest Priority Pending Interrupt (Group 0)
#define ICC_HPPIR1_EL1      "S3_0_C12_C12_2"   // Highest Priority Pending Interrupt (Group 1)
#define ICC_BPR0_EL1        "S3_0_C12_C8_3"    // Binary Point Register (Group 0)
#define ICC_BPR1_EL1        "S3_0_C12_C12_3"   // Binary Point Register (Group 1)
#define ICC_DIR_EL1         "S3_0_C12_C11_1"   // Deactivate Interrupt Register
#define ICC_RPR_EL1         "S3_0_C12_C11_3"   // Running Priority Register
#define ICC_SGI1R_EL1       "S3_0_C12_C11_5"   // Software Generated Interrupt Register
#define ICC_ASGI1R_EL1      "S3_0_C12_C11_6"   // Alias Software Generated Interrupt Register
#define ICC_SGI0R_EL1       "S3_0_C12_C11_7"   // Software Generated Interrupt Register (Group 0)
#define ICC_CTLR_EL1        "S3_0_C12_C12_4"   // Control Register (EL1)
#define ICC_SRE_EL1         "S3_0_C12_C12_5"   // System Register Enable (EL1)
#define ICC_IGRPEN0_EL1     "S3_0_C12_C12_6"   // Interrupt Group 0 Enable
#define ICC_IGRPEN1_EL1     "S3_0_C12_C12_7"   // Interrupt Group 1 Enable

// EL2 registers (for future hypervisor support)
#define ICC_SRE_EL2         "S3_4_C12_C9_5"    // System Register Enable (EL2)

// EL3 registers (for secure world, if needed)
#define ICC_CTLR_EL3        "S3_6_C12_C12_4"   // Control Register (EL3)
#define ICC_SRE_EL3         "S3_6_C12_C12_5"   // System Register Enable (EL3)
#define ICC_IGRPEN1_EL3     "S3_6_C12_C12_7"   // Interrupt Group 1 Enable (EL3)

// ICC_CTLR_EL1 bit definitions
#define ICC_CTLR_EL1_EOImode    (1U << 1)      // EOI mode (split EOI/deactivate)
#define ICC_CTLR_EL1_CBPR       (1U << 0)      // Common Binary Point Register

// ICC_SRE_EL1 bit definitions
#define ICC_SRE_EL1_SRE         (1U << 0)      // System Register Enable
#define ICC_SRE_EL1_DFB         (1U << 1)      // Disable FIQ Bypass
#define ICC_SRE_EL1_DIB         (1U << 2)      // Disable IRQ Bypass
#define ICC_SRE_EL1_Enable      (1U << 3)      // Enable lower EL access

// ICC_IGRPEN1_EL1 bit definitions
#define ICC_IGRPEN1_EL1_Enable  (1U << 0)      // Enable Group 1 interrupts

// SGI target definitions for ICC_SGI1R_EL1
#define ICC_SGI1R_TARGET_LIST_SHIFT    0
#define ICC_SGI1R_AFF1_SHIFT           16
#define ICC_SGI1R_INTID_SHIFT          24
#define ICC_SGI1R_AFF2_SHIFT           32
#define ICC_SGI1R_IRM                  (1ULL << 40)  // Interrupt Routing Mode
#define ICC_SGI1R_AFF3_SHIFT           48

// Helper macros for system register access
#define gic_sysreg_read(reg) ({                    \
    uint64_t __val;                                \
    __asm__ volatile("mrs %0, " reg                \
                     : "=r" (__val));               \
    __val;                                          \
})

#define gic_sysreg_write(reg, val) ({              \
    uint64_t __val = (uint64_t)(val);              \
    __asm__ volatile("msr " reg ", %0"             \
                     :: "r" (__val));               \
})

// Specific register access functions for common operations
static inline uint32_t gicv3_read_iar1(void) {
    return (uint32_t)gic_sysreg_read(ICC_IAR1_EL1);
}

static inline void gicv3_write_eoir1(uint32_t irq) {
    gic_sysreg_write(ICC_EOIR1_EL1, irq);
}

static inline void gicv3_write_pmr(uint32_t priority) {
    gic_sysreg_write(ICC_PMR_EL1, priority);
}

static inline uint32_t gicv3_read_pmr(void) {
    return (uint32_t)gic_sysreg_read(ICC_PMR_EL1);
}

static inline void gicv3_write_ctlr(uint32_t val) {
    gic_sysreg_write(ICC_CTLR_EL1, val);
}

static inline uint32_t gicv3_read_ctlr(void) {
    return (uint32_t)gic_sysreg_read(ICC_CTLR_EL1);
}

static inline void gicv3_write_grpen1(uint32_t val) {
    gic_sysreg_write(ICC_IGRPEN1_EL1, val);
}

static inline uint32_t gicv3_read_grpen1(void) {
    return (uint32_t)gic_sysreg_read(ICC_IGRPEN1_EL1);
}

static inline void gicv3_write_sre(uint32_t val) {
    gic_sysreg_write(ICC_SRE_EL1, val);
}

static inline uint32_t gicv3_read_sre(void) {
    return (uint32_t)gic_sysreg_read(ICC_SRE_EL1);
}

static inline void gicv3_write_sgi1r(uint64_t val) {
    gic_sysreg_write(ICC_SGI1R_EL1, val);
}

static inline void gicv3_write_dir(uint32_t irq) {
    gic_sysreg_write(ICC_DIR_EL1, irq);
}

static inline void gicv3_write_bpr1(uint32_t val) {
    gic_sysreg_write(ICC_BPR1_EL1, val);
}

// Memory barrier operations for GIC
static inline void gic_wmb(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

static inline void gic_rmb(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

// MPIDR_EL1 register access (for affinity routing)
#define MPIDR_EL1           "S3_0_C0_C0_5"

static inline uint64_t read_mpidr(void) {
    return gic_sysreg_read(MPIDR_EL1);
}

// MPIDR affinity level extraction
#define MPIDR_AFF0_MASK     0xFF
#define MPIDR_AFF1_MASK     0xFF00
#define MPIDR_AFF2_MASK     0xFF0000
#define MPIDR_AFF3_MASK     0xFF00000000

#define MPIDR_AFF0_SHIFT    0
#define MPIDR_AFF1_SHIFT    8
#define MPIDR_AFF2_SHIFT    16
#define MPIDR_AFF3_SHIFT    32

#define MPIDR_AFFINITY_MASK 0x00FFFFFF  // Aff0-2 only, not Aff3 for GICv3

static inline uint32_t mpidr_to_affinity(uint64_t mpidr) {
    return (uint32_t)(mpidr & MPIDR_AFFINITY_MASK);
}

#endif /* __aarch64__ */

#endif /* _ARCH_GIC_SYSREG_H_ */