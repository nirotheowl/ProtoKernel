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
#include <drivers/fdt.h>

// Generic page size constants
#define PAGE_SHIFT              12
#define PAGE_SIZE               (1UL << PAGE_SHIFT)    // 4KB
#define PAGE_MASK               (~(PAGE_SIZE - 1))

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
void vmm_create_dmap(memory_info_t *mem_info);

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

/* Helper functions for page table address conversion */
void* vmm_pt_phys_to_virt(uint64_t phys);
uint64_t vmm_pt_virt_to_phys(void *virt);

#endif /* _VMM_H_ */