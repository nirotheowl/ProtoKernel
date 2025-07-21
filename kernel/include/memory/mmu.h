#ifndef MMU_H
#define MMU_H

#include <stdint.h>

// Virtual address bits for ARM64
#define VA_BITS                 48

// MAIR_EL1 attribute encoding
#define MAIR_ATTR_DEVICE_nGnRnE 0x00ULL  // Device, non-gathering, non-reordering, no early write ack
#define MAIR_ATTR_DEVICE_nGnRE  0x04ULL  // Device, non-gathering, non-reordering, early write ack
#define MAIR_ATTR_DEVICE_GRE    0x0CULL  // Device, gathering, reordering, early write ack
#define MAIR_ATTR_NORMAL_NC     0x44ULL  // Normal, non-cacheable
#define MAIR_ATTR_NORMAL_WT     0xBBULL  // Normal, write-through cacheable
#define MAIR_ATTR_NORMAL        0xFFULL  // Normal, write-back cacheable

// TCR_EL1 fields
#define TCR_T0SZ(x)             ((64UL - (x)) << 0)
#define TCR_T1SZ(x)             ((64UL - (x)) << 16)
#define TCR_EPD0_ENABLE         (0UL << 7)
#define TCR_EPD1_DISABLE        (1UL << 23)
#define TCR_TG0_4K              (0UL << 14)
#define TCR_TG1_4K              (2UL << 30)
#define TCR_SH0_NONE            (0UL << 12)
#define TCR_SH0_OUTER           (2UL << 12)
#define TCR_SH0_INNER           (3UL << 12)
#define TCR_SH1_NONE            (0UL << 28)
#define TCR_SH1_OUTER           (2UL << 28)
#define TCR_SH1_INNER           (3UL << 28)
#define TCR_ORGN0_NC            (0UL << 10)
#define TCR_ORGN0_WBWA          (1UL << 10)
#define TCR_ORGN0_WT            (2UL << 10)
#define TCR_ORGN0_WBNWA         (3UL << 10)
#define TCR_ORGN1_NC            (0UL << 26)
#define TCR_ORGN1_WBWA          (1UL << 26)
#define TCR_ORGN1_WT            (2UL << 26)
#define TCR_ORGN1_WBNWA         (3UL << 26)
#define TCR_IRGN0_NC            (0UL << 8)
#define TCR_IRGN0_WBWA          (1UL << 8)
#define TCR_IRGN0_WT            (2UL << 8)
#define TCR_IRGN0_WBNWA         (3UL << 8)
#define TCR_IRGN1_NC            (0UL << 24)
#define TCR_IRGN1_WBWA          (1UL << 24)
#define TCR_IRGN1_WT            (2UL << 24)
#define TCR_IRGN1_WBNWA         (3UL << 24)
#define TCR_IPS_32BIT           (0UL << 32)
#define TCR_IPS_36BIT           (1UL << 32)
#define TCR_IPS_40BIT           (2UL << 32)
#define TCR_IPS_42BIT           (3UL << 32)
#define TCR_IPS_44BIT           (4UL << 32)
#define TCR_IPS_48BIT           (5UL << 32)
#define TCR_IPS_52BIT           (6UL << 32)
#define TCR_AS_16BIT            (1UL << 36)
#define TCR_TBI0_IGNORED        (1UL << 37)
#define TCR_TBI1_IGNORED        (1UL << 38)
#define TCR_HA                  (1UL << 39)
#define TCR_HD                  (1UL << 40)
#define TCR_HPD0                (1UL << 41)
#define TCR_HPD1                (1UL << 42)
#define TCR_HWU059              (1UL << 43)
#define TCR_HWU060              (1UL << 44)
#define TCR_HWU061              (1UL << 45)
#define TCR_HWU062              (1UL << 46)
#define TCR_HWU159              (1UL << 47)
#define TCR_HWU160              (1UL << 48)
#define TCR_HWU161              (1UL << 49)
#define TCR_HWU162              (1UL << 50)
#define TCR_TBID0               (1UL << 51)
#define TCR_TBID1               (1UL << 52)
#define TCR_NFD0                (1UL << 53)
#define TCR_NFD1                (1UL << 54)

// SCTLR_EL1 fields
#define SCTLR_M                 (1UL << 0)   // MMU enable
#define SCTLR_A                 (1UL << 1)   // Alignment check
#define SCTLR_C                 (1UL << 2)   // Cache enable
#define SCTLR_SA                (1UL << 3)   // Stack alignment check
#define SCTLR_SA0               (1UL << 4)   // Stack alignment check for EL0
#define SCTLR_CP15BEN           (1UL << 5)   // CP15 barrier enable
#define SCTLR_nAA               (1UL << 6)   // Non-aligned access
#define SCTLR_ITD               (1UL << 7)   // IT disable
#define SCTLR_SED               (1UL << 8)   // SETEND disable
#define SCTLR_UMA               (1UL << 9)   // User mask access
#define SCTLR_EnRCTX            (1UL << 10)  // Enable EL0 access to RC
#define SCTLR_EOS               (1UL << 11)  // Exception origin
#define SCTLR_I                 (1UL << 12)  // I-cache enable
#define SCTLR_EnDB              (1UL << 13)  // Enable pointer auth (data B)
#define SCTLR_DZE               (1UL << 14)  // DC ZVA enable
#define SCTLR_UCT               (1UL << 15)  // CTR_EL0 access
#define SCTLR_nTWI              (1UL << 16)  // Not trap WFI
#define SCTLR_nTWE              (1UL << 18)  // Not trap WFE
#define SCTLR_WXN               (1UL << 19)  // Write implies XN
#define SCTLR_TSCXT             (1UL << 20)  // Trap special context
#define SCTLR_IESB              (1UL << 21)  // Implicit error sync
#define SCTLR_EIS               (1UL << 22)  // Exception IS
#define SCTLR_SPAN              (1UL << 23)  // Set PAN
#define SCTLR_E0E               (1UL << 24)  // Endianness EL0
#define SCTLR_EE                (1UL << 25)  // Endianness EL1
#define SCTLR_UCI               (1UL << 26)  // Cache instruction perm
#define SCTLR_EnDA              (1UL << 27)  // Enable pointer auth (data A)
#define SCTLR_nTLSMD            (1UL << 28)  // No trap load multiple
#define SCTLR_LSMAOE            (1UL << 29)  // Load multiple atomicity
#define SCTLR_EnIB              (1UL << 30)  // Enable pointer auth (insn B)
#define SCTLR_EnIA              (1UL << 31)  // Enable pointer auth (insn A)

// Memory barrier definitions
#define dsb(opt)                __asm__ volatile("dsb " #opt : : : "memory")
#define dmb(opt)                __asm__ volatile("dmb " #opt : : : "memory")
#define isb()                   __asm__ volatile("isb" : : : "memory")

// TLB maintenance operations
#define tlbi(op)                __asm__ volatile("tlbi " #op : : : "memory")

// Cache maintenance operations
#define dc_civac(addr)          __asm__ volatile("dc civac, %0" : : "r" (addr) : "memory")
#define dc_cvac(addr)           __asm__ volatile("dc cvac, %0" : : "r" (addr) : "memory")
#define dc_cvau(addr)           __asm__ volatile("dc cvau, %0" : : "r" (addr) : "memory")
#define dc_ivac(addr)           __asm__ volatile("dc ivac, %0" : : "r" (addr) : "memory")
#define dc_zva(addr)            __asm__ volatile("dc zva, %0" : : "r" (addr) : "memory")
#define ic_iallu()              __asm__ volatile("ic iallu" : : : "memory")
#define ic_ialluis()            __asm__ volatile("ic ialluis" : : : "memory")
#define ic_ivau(addr)           __asm__ volatile("ic ivau, %0" : : "r" (addr) : "memory")

// Function declarations
void mmu_init(void);
void enable_mmu(void);
void disable_mmu(void);

// Cache management functions
void dcache_clean_range(uint64_t start, uint64_t size);
void dcache_invalidate_range(uint64_t start, uint64_t size);
void dcache_clean_invalidate_range(uint64_t start, uint64_t size);
void icache_invalidate_all(void);

// TLB management functions
void tlb_invalidate_all(void);
void tlb_invalidate_page(uint64_t va);
void tlb_invalidate_range(uint64_t va, uint64_t size);

#endif // MMU_H