/*
 * arch/riscv/include/mm/pte.h
 *
 * RISC-V Page Table Entry definitions for Sv39
 */

#ifndef _RISCV_PTE_H_
#define _RISCV_PTE_H_

#include <stdint.h>

/* RISC-V Sv39 Page Table Entry format */
#define RISCV_PTE_V     (1UL << 0)   /* Valid */
#define RISCV_PTE_R     (1UL << 1)   /* Read permission */
#define RISCV_PTE_W     (1UL << 2)   /* Write permission */
#define RISCV_PTE_X     (1UL << 3)   /* Execute permission */
#define RISCV_PTE_U     (1UL << 4)   /* User accessible */
#define RISCV_PTE_G     (1UL << 5)   /* Global mapping */
#define RISCV_PTE_A     (1UL << 6)   /* Accessed */
#define RISCV_PTE_D     (1UL << 7)   /* Dirty */
#define RISCV_PTE_RSW   (3UL << 8)   /* Reserved for software (2 bits) */

/* Physical Page Number (PPN) fields in Sv39 */
#define RISCV_PTE_PPN_SHIFT     10
#define RISCV_PTE_PPN_MASK      0x3FFFFFFFFFFC00UL  /* Bits [53:10] */

/* Extract physical address from PTE */
#define RISCV_PTE_TO_PHYS(pte)  (((pte) & RISCV_PTE_PPN_MASK) << 2)

/* Create PTE from physical address */
#define RISCV_PHYS_TO_PTE(phys) (((phys) >> 2) & RISCV_PTE_PPN_MASK)

/* Page table entry types */
#define RISCV_PTE_TYPE_INVALID  0                           /* Invalid entry */
#define RISCV_PTE_TYPE_TABLE    RISCV_PTE_V                /* Points to next level */
#define RISCV_PTE_TYPE_LEAF     (RISCV_PTE_V | RISCV_PTE_R | RISCV_PTE_X | RISCV_PTE_W)  /* Any RWX bit set */

/* Check PTE type */
#define RISCV_PTE_IS_VALID(pte)     ((pte) & RISCV_PTE_V)
#define RISCV_PTE_IS_LEAF(pte)      (((pte) & RISCV_PTE_V) && ((pte) & (RISCV_PTE_R | RISCV_PTE_W | RISCV_PTE_X)))
#define RISCV_PTE_IS_TABLE(pte)     (((pte) & RISCV_PTE_V) && !((pte) & (RISCV_PTE_R | RISCV_PTE_W | RISCV_PTE_X)))

/* Sv39 virtual address format */
#define RISCV_VA_VPN2_SHIFT     30  /* Level 2 VPN */
#define RISCV_VA_VPN1_SHIFT     21  /* Level 1 VPN */
#define RISCV_VA_VPN0_SHIFT     12  /* Level 0 VPN */
#define RISCV_VA_VPN_MASK       0x1FF  /* 9 bits per VPN */

/* Page and block sizes */
#define RISCV_PAGE_SHIFT        12
#define RISCV_PAGE_SIZE         (1UL << RISCV_PAGE_SHIFT)      /* 4KB */
#define RISCV_MEGAPAGE_SHIFT    21
#define RISCV_MEGAPAGE_SIZE     (1UL << RISCV_MEGAPAGE_SHIFT)  /* 2MB */
#define RISCV_GIGAPAGE_SHIFT    30
#define RISCV_GIGAPAGE_SIZE     (1UL << RISCV_GIGAPAGE_SHIFT)  /* 1GB */

/* Number of PTEs per page table */
#define RISCV_PTRS_PER_TABLE    512

/* Sv39 page table levels */
#define RISCV_PT_LEVELS         3
#define RISCV_PT_LEVEL_2        2    /* Top level (PGD) - 1GB pages */
#define RISCV_PT_LEVEL_1        1    /* Middle level (PMD) - 2MB pages */
#define RISCV_PT_LEVEL_0        0    /* Leaf level (PTE) - 4KB pages */

/* SATP register format for Sv39 */
#define SATP_MODE_SHIFT         60
#define SATP_MODE_SV39          (8UL << SATP_MODE_SHIFT)
#define SATP_ASID_SHIFT         44
#define SATP_ASID_MASK          0xFFFF
#define SATP_PPN_MASK           0xFFFFFFFFFFF

/* Helper macros for SATP */
#define SATP_MAKE(mode, asid, ppn) \
    ((mode) | ((uint64_t)(asid) << SATP_ASID_SHIFT) | ((ppn) >> 12))

/* Memory attributes for RISC-V (simplified) */
#define RISCV_PTE_NORMAL        (RISCV_PTE_R | RISCV_PTE_W | RISCV_PTE_A | RISCV_PTE_D)
#define RISCV_PTE_NORMAL_RO     (RISCV_PTE_R | RISCV_PTE_A)
#define RISCV_PTE_NORMAL_EXEC   (RISCV_PTE_R | RISCV_PTE_X | RISCV_PTE_A)
#define RISCV_PTE_NORMAL_RWX    (RISCV_PTE_R | RISCV_PTE_W | RISCV_PTE_X | RISCV_PTE_A | RISCV_PTE_D)
#define RISCV_PTE_DEVICE        (RISCV_PTE_R | RISCV_PTE_W | RISCV_PTE_A | RISCV_PTE_D)

#endif /* _RISCV_PTE_H_ */