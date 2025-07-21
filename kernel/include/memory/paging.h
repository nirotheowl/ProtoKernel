#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// Page sizes
#define PAGE_SHIFT              12
#define PAGE_SIZE               (1UL << PAGE_SHIFT)    // 4KB
#define PAGE_MASK               (~(PAGE_SIZE - 1))

// Number of entries per page table
#define PTRS_PER_TABLE          512

// Virtual address breakdown for 48-bit addressing
#define VA_BITS                 48
#define PGDIR_SHIFT             39  // Level 0
#define PUD_SHIFT               30  // Level 1  
#define PMD_SHIFT               21  // Level 2
#define PTE_SHIFT               12  // Level 3

// Page table entry types
#define PTE_TYPE_MASK           (3UL << 0)
#define PTE_TYPE_FAULT          (0UL << 0)
#define PTE_TYPE_TABLE          (3UL << 0)  // Table descriptor (bits 1:0 = 11)
#define PTE_TYPE_PAGE           (3UL << 0)  // Page descriptor (bits 1:0 = 11)
#define PTE_TYPE_BLOCK          (1UL << 0)  // Block descriptor (bits 1:0 = 01)

// Page table entry attributes
#define PTE_VALID               (1UL << 0)
#define PTE_TABLE               (1UL << 1)  // 1 = Table, 0 = Block
#define PTE_ATTRINDX(n)         ((n) << 2)  // MAIR index
#define PTE_NS                  (1UL << 5)  // Non-secure
#define PTE_AP_RW               (0UL << 6)  // Read-write, EL0 no access
#define PTE_AP_RW_EL0           (1UL << 6)  // Read-write, EL0 read-write
#define PTE_AP_RO               (2UL << 6)  // Read-only, EL0 no access
#define PTE_AP_RO_EL0           (3UL << 6)  // Read-only, EL0 read-only
#define PTE_SH_NONE             (0UL << 8)  // Non-shareable
#define PTE_SH_OUTER            (2UL << 8)  // Outer shareable
#define PTE_SH_INNER            (3UL << 8)  // Inner shareable
#define PTE_AF                  (1UL << 10) // Access flag
#define PTE_nG                  (1UL << 11) // Not global
#define PTE_DBM                 (1UL << 51) // Dirty bit modifier
#define PTE_CONT                (1UL << 52) // Contiguous hint
#define PTE_PXN                 (1UL << 53) // Privileged execute never
#define PTE_UXN                 (1UL << 54) // User execute never
#define PTE_XN                  (1UL << 54) // Execute never (alias)

// Memory attributes for MAIR_EL1
#define MT_DEVICE_nGnRnE        0   // Device memory
#define MT_DEVICE_nGnRE         1   // Device memory with early ack
#define MT_NORMAL_NC            2   // Normal non-cacheable
#define MT_NORMAL               3   // Normal write-back cacheable

// Common page attributes combinations
#define PTE_KERNEL_PAGE         (PTE_VALID | PTE_TYPE_PAGE | PTE_AF | \
                                PTE_SH_INNER | PTE_ATTRINDX(MT_NORMAL))

#define PTE_KERNEL_BLOCK        (PTE_VALID | PTE_TYPE_BLOCK | PTE_AF | \
                                PTE_SH_INNER | PTE_ATTRINDX(MT_NORMAL))

#define PTE_DEVICE_BLOCK        (PTE_VALID | PTE_TYPE_BLOCK | PTE_AF | \
                                PTE_ATTRINDX(MT_DEVICE_nGnRnE))

// Helper macros
#define PGDIR_INDEX(va)         (((va) >> PGDIR_SHIFT) & (PTRS_PER_TABLE - 1))
#define PUD_INDEX(va)           (((va) >> PUD_SHIFT) & (PTRS_PER_TABLE - 1))
#define PMD_INDEX(va)           (((va) >> PMD_SHIFT) & (PTRS_PER_TABLE - 1))
#define PTE_INDEX(va)           (((va) >> PTE_SHIFT) & (PTRS_PER_TABLE - 1))

// Page table structure
typedef uint64_t pte_t;
typedef uint64_t pmd_t;
typedef uint64_t pud_t;
typedef uint64_t pgd_t;

// Function declarations for paging operations
void map_page(uint64_t va, uint64_t pa, uint64_t attrs);
void map_range(uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs);
void paging_init(void);

// Get the kernel page directory base address
pgd_t* get_kernel_pgd(void);

#endif // PAGING_H