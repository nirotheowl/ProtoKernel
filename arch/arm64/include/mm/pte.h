/*
 * arch/arm64/include/mm/pte.h
 *
 * ARM64 Page Table Entry definitions
 */

#ifndef _ARM64_PTE_H_
#define _ARM64_PTE_H_

#include <stdint.h>

/* ARM64 Page Table Entry format */
#define ARM64_PTE_VALID         (1UL << 0)   /* Valid/Present bit */
#define ARM64_PTE_TABLE         (1UL << 1)   /* Table descriptor (non-leaf) */
#define ARM64_PTE_TYPE_MASK     (3UL << 0)   /* Entry type mask */
#define ARM64_PTE_TYPE_BLOCK    (1UL << 0)   /* Block descriptor */
#define ARM64_PTE_TYPE_TABLE    (3UL << 0)   /* Table descriptor */
#define ARM64_PTE_TYPE_PAGE     (3UL << 0)   /* Page descriptor (L3 only) */

/* Memory attributes */
#define ARM64_PTE_ATTRINDX(n)   ((n) << 2)   /* Memory Attribute Index */
#define ARM64_PTE_NS            (1UL << 5)   /* Non-Secure */
#define ARM64_PTE_AP_RW         (0UL << 6)   /* EL1 RW, EL0 no access */
#define ARM64_PTE_AP_RO         (2UL << 6)   /* EL1 RO, EL0 no access */
#define ARM64_PTE_AP_RW_ALL     (1UL << 6)   /* EL1 RW, EL0 RW */
#define ARM64_PTE_AP_RO_ALL     (3UL << 6)   /* EL1 RO, EL0 RO */
#define ARM64_PTE_AP_MASK       (3UL << 6)   /* Access Permission mask */

/* Shareability */
#define ARM64_PTE_SH_NONE       (0UL << 8)   /* Non-shareable */
#define ARM64_PTE_SH_OUTER      (2UL << 8)   /* Outer shareable */
#define ARM64_PTE_SH_INNER      (3UL << 8)   /* Inner shareable */

/* Access Flag and other attributes */
#define ARM64_PTE_AF            (1UL << 10)  /* Access Flag */
#define ARM64_PTE_NG            (1UL << 11)  /* Not Global */
#define ARM64_PTE_CONT          (1UL << 52)  /* Contiguous */
#define ARM64_PTE_PXN           (1UL << 53)  /* Privileged Execute Never */
#define ARM64_PTE_UXN           (1UL << 54)  /* User Execute Never */

/* Physical address mask */
#define ARM64_PTE_ADDR_MASK     0x0000FFFFFFFFF000UL  /* Bits [47:12] for address */

/* Extract physical address from PTE */
#define ARM64_PTE_TO_PHYS(pte)  ((pte) & ARM64_PTE_ADDR_MASK)

/* Create PTE from physical address */
#define ARM64_PHYS_TO_PTE(phys) ((phys) & ARM64_PTE_ADDR_MASK)

/* Check PTE type */
#define ARM64_PTE_IS_VALID(pte)     ((pte) & ARM64_PTE_VALID)
#define ARM64_PTE_IS_TABLE(pte)     (((pte) & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_TABLE)
#define ARM64_PTE_IS_BLOCK(pte)     (((pte) & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)

/* Memory type definitions (MAIR indices) */
#define MT_DEVICE_nGnRnE        0  /* Device, non-gathering, non-reordering, no early ack */
#define MT_DEVICE_nGnRE         1  /* Device, non-gathering, non-reordering, early ack */
#define MT_NORMAL_NC            2  /* Normal, non-cacheable */
#define MT_NORMAL               3  /* Normal, cacheable */

/* Virtual address format for 4KB pages with 4-level translation */
#define ARM64_VA_L0_SHIFT       39  /* Level 0 index */
#define ARM64_VA_L1_SHIFT       30  /* Level 1 index */
#define ARM64_VA_L2_SHIFT       21  /* Level 2 index */
#define ARM64_VA_L3_SHIFT       12  /* Level 3 index */
#define ARM64_VA_INDEX_MASK     0x1FF  /* 9 bits per level */

/* Page and block sizes */
#define ARM64_PAGE_SHIFT        12
#define ARM64_PAGE_SIZE         (1UL << ARM64_PAGE_SHIFT)      /* 4KB */
#define ARM64_BLOCK_SHIFT_L2    21
#define ARM64_BLOCK_SIZE_L2     (1UL << ARM64_BLOCK_SHIFT_L2)  /* 2MB */
#define ARM64_BLOCK_SHIFT_L1    30
#define ARM64_BLOCK_SIZE_L1     (1UL << ARM64_BLOCK_SHIFT_L1)  /* 1GB */

/* Number of PTEs per page table */
#define ARM64_PTRS_PER_TABLE    512

/* Page table levels for 4KB pages */
#define ARM64_PT_LEVELS         4
#define ARM64_PT_LEVEL_0        0    /* Top level - 512GB per entry */
#define ARM64_PT_LEVEL_1        1    /* 1GB per entry */
#define ARM64_PT_LEVEL_2        2    /* 2MB per entry */
#define ARM64_PT_LEVEL_3        3    /* Leaf level - 4KB per entry */

/* Memory attributes for common cases */
#define ARM64_PTE_ATTR_DEVICE   ARM64_PTE_ATTRINDX(MT_DEVICE_nGnRnE)
#define ARM64_PTE_ATTR_NORMAL_NC ARM64_PTE_ATTRINDX(MT_NORMAL_NC)
#define ARM64_PTE_ATTR_NORMAL   ARM64_PTE_ATTRINDX(MT_NORMAL)

/* Common PTE flag combinations */
#define ARM64_PTE_NORMAL_RW     (ARM64_PTE_AF | ARM64_PTE_SH_INNER | ARM64_PTE_ATTR_NORMAL | ARM64_PTE_AP_RW)
#define ARM64_PTE_NORMAL_RO     (ARM64_PTE_AF | ARM64_PTE_SH_INNER | ARM64_PTE_ATTR_NORMAL | ARM64_PTE_AP_RO)
#define ARM64_PTE_NORMAL_EXEC   (ARM64_PTE_AF | ARM64_PTE_SH_INNER | ARM64_PTE_ATTR_NORMAL | ARM64_PTE_AP_RO)
#define ARM64_PTE_DEVICE_RW     (ARM64_PTE_AF | ARM64_PTE_ATTR_DEVICE | ARM64_PTE_AP_RW | ARM64_PTE_UXN | ARM64_PTE_PXN)

#endif /* _ARM64_PTE_H_ */