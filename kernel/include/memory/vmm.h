/*
 * kernel/include/memory/vmm.h
 *
 * Virtual Memory Manager interface
 * Manages virtual address space and page table operations
 */

#ifndef _VMM_H_
#define _VMM_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Page sizes and constants from paging.h */
#define PAGE_SHIFT              12
#define PAGE_SIZE               (1UL << PAGE_SHIFT)    // 4KB
#define PAGE_MASK               (~(PAGE_SIZE - 1))
#define SECTION_SHIFT           21
#define SECTION_SIZE            (1UL << SECTION_SHIFT) // 2MB
#define SECTION_MASK            (~(SECTION_SIZE - 1))
#define PTRS_PER_TABLE          512

/* Page table entry types and attributes */
#define PTE_VALID               (1UL << 0)
#define PTE_TABLE               (1UL << 1)
#define PTE_TYPE_BLOCK          (1UL << 0)  /* Block descriptor */
#define PTE_TYPE_TABLE          (3UL << 0)  /* Table descriptor */
#define PTE_TYPE_PAGE           (3UL << 0)  /* Page descriptor */
#define PTE_ADDR_MASK           0x0000FFFFFFFFF000UL
#define PTE_ATTRINDX(n)         ((n) << 2)
#define PTE_NS                  (1UL << 5)
#define PTE_AP_RW               (0UL << 6)
#define PTE_AP_RO               (2UL << 6)
#define PTE_SH_INNER            (3UL << 8)
#define PTE_SH_OUTER            (2UL << 8)
#define PTE_AF                  (1UL << 10)
#define PTE_PXN                 (1UL << 53)
#define PTE_UXN                 (1UL << 54)

/* Memory type definitions (MAIR indices) */
#define MT_DEVICE_nGnRnE        0  /* Device, non-gathering, non-reordering, no early ack */
#define MT_DEVICE_nGnRE         1  /* Device, non-gathering, non-reordering, early ack */
#define MT_NORMAL_NC            2  /* Normal, non-cacheable */
#define MT_NORMAL               3  /* Normal, cacheable */

/* Page table levels */
#define PT_LEVEL_0  0  /* 512GB per entry */
#define PT_LEVEL_1  1  /* 1GB per entry */
#define PT_LEVEL_2  2  /* 2MB per entry */
#define PT_LEVEL_3  3  /* 4KB per entry */

/* Memory attributes */
#define VMM_ATTR_READ      (1UL << 0)
#define VMM_ATTR_WRITE     (1UL << 1)
#define VMM_ATTR_EXECUTE   (1UL << 2)
#define VMM_ATTR_USER      (1UL << 3)
#define VMM_ATTR_DEVICE    (1UL << 4)
#define VMM_ATTR_NOCACHE   (1UL << 5)

/* Common attribute combinations */
#define VMM_ATTR_RW        (VMM_ATTR_READ | VMM_ATTR_WRITE)
#define VMM_ATTR_RX        (VMM_ATTR_READ | VMM_ATTR_EXECUTE)
#define VMM_ATTR_RWX       (VMM_ATTR_READ | VMM_ATTR_WRITE | VMM_ATTR_EXECUTE)

/* Page table structure */
typedef struct {
    uint64_t *l0_table;     /* Level 0 (PGD) table */
    uint64_t phys_base;     /* Physical base of page tables */
    bool is_kernel;         /* True for kernel (TTBR1), false for user (TTBR0) */
} vmm_context_t;

/* Function prototypes */

/* Initialize VMM subsystem */
void vmm_init(void);

/* Allocate a new page table from PMM */
uint64_t* vmm_alloc_page_table(void);

/* Free a page table back to PMM */
void vmm_free_page_table(uint64_t *table);

/* Map a single page with given attributes */
bool vmm_map_page(vmm_context_t *ctx, uint64_t vaddr, uint64_t paddr, uint64_t attrs);

/* Map a range of pages (optimizes for large pages when possible) */
bool vmm_map_range(vmm_context_t *ctx, uint64_t vaddr, uint64_t paddr, 
                   size_t size, uint64_t attrs);

/* Unmap a single page */
bool vmm_unmap_page(vmm_context_t *ctx, uint64_t vaddr);

/* Unmap a range of pages */
bool vmm_unmap_range(vmm_context_t *ctx, uint64_t vaddr, size_t size);

/* Create the DMAP region for all physical memory */
void vmm_create_dmap(void);

/* Get current kernel page table context */
vmm_context_t* vmm_get_kernel_context(void);

/* Walk page tables and return physical address for a virtual address */
uint64_t vmm_virt_to_phys(vmm_context_t *ctx, uint64_t vaddr);

/* Check if a virtual address is mapped */
bool vmm_is_mapped(vmm_context_t *ctx, uint64_t vaddr);

/* Flush TLB for a specific address */
void vmm_flush_tlb_page(uint64_t vaddr);

/* Flush entire TLB */
void vmm_flush_tlb_all(void);

/* Debug: print page table entries for an address */
void vmm_debug_walk(vmm_context_t *ctx, uint64_t vaddr);

/* Check if DMAP is ready for use */
bool vmm_is_dmap_ready(void);

#endif /* _VMM_H_ */