#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

// Page size is 4KB
#define PMM_PAGE_SIZE 4096
#define PMM_PAGE_SHIFT 12

// Forward declaration - actual definition in memmap.h
typedef struct mem_region mem_region_t;

// Statistics
typedef struct pmm_stats {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t reserved_pages;
    uint64_t allocated_pages;
    uint64_t page_table_pages;
} pmm_stats_t;

// Initialize the physical memory manager
// Note: memory_info_t is defined in drivers/fdt.h
struct memory_info;
void pmm_init(uint64_t kernel_end, struct memory_info *mem_info);

// Allocate a single page (returns physical address)
uint64_t pmm_alloc_page(void);

// Allocate a single page for page table use
uint64_t pmm_alloc_page_table(void);

// Allocate multiple contiguous pages
uint64_t pmm_alloc_pages(size_t count);

// Free a page
void pmm_free_page(uint64_t pa);

// Free multiple contiguous pages
void pmm_free_pages(uint64_t pa, size_t count);

// Mark region as reserved
void pmm_reserve_region(uint64_t base, uint64_t size, const char* name);

// Reserve a single page
void pmm_reserve_page(uint64_t pa);

// Get memory statistics
void pmm_get_stats(pmm_stats_t* stats);

// Check if address is available
int pmm_is_available(uint64_t pa);

// Get end of managed physical memory
uint64_t pmm_get_memory_end(void);

// Print memory statistics
void pmm_print_stats(void);

#endif // PMM_H
