/*
 * kernel/memory/pmm.c
 *
 * Physical memory manager implementation
 */

#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <memory/vmm.h>
#include <memory/pmm_bootstrap.h>
#include <drivers/fdt.h>
#include <uart.h>
#include <stdint.h>
#include <stdbool.h>
#include <boot_config.h>

// External symbols from linker script
extern char _kernel_end;

// Static array of memory regions
static pmm_region_t pmm_regions[PMM_MAX_REGIONS];
static int pmm_region_count = 0;

// Memory statistics
static pmm_stats_t pmm_stats = {0};

// Initialization flag
static bool pmm_initialized = false;

// Helper functions for bitmap operations
static void pmm_set_bit(pmm_region_t *region, uint64_t page_num) {
    region->bitmap[page_num / 64] |= (1ULL << (page_num % 64));
}

static void pmm_clear_bit(pmm_region_t *region, uint64_t page_num) {
    region->bitmap[page_num / 64] &= ~(1ULL << (page_num % 64));
}

static int pmm_test_bit(pmm_region_t *region, uint64_t page_num) {
    return (region->bitmap[page_num / 64] & (1ULL << (page_num % 64))) != 0;
}

static uint64_t addr_to_page(pmm_region_t *region, uint64_t addr) {
    return (addr - region->base) >> PMM_PAGE_SHIFT;
}

static uint64_t page_to_addr(pmm_region_t *region, uint64_t page) {
    return region->base + (page << PMM_PAGE_SHIFT);
}

// Find which region contains a physical address
static pmm_region_t* pmm_find_region(uint64_t pa) {
    for (int i = 0; i < pmm_region_count; i++) {
        pmm_region_t *region = &pmm_regions[i];
        if (pa >= region->base && pa < region->base + region->size) {
            return region;
        }
    }
    return NULL;
}

// Initialize the physical memory manager
void pmm_init(uint64_t kernel_end, struct memory_info *mem_info) {
    memory_info_t *info = (memory_info_t *)mem_info;
    if (!info || info->count == 0) {
        uart_puts("ERROR: No memory information provided to PMM\n");
        return;
    }
    
    // kernel_end is a virtual address, convert to physical
    uint64_t kernel_end_phys = VIRT_TO_PHYS(kernel_end);
    
    // Calculate total bitmap size needed for all regions
    uint64_t total_bitmap_size = 0;
    for (int i = 0; i < info->count && i < PMM_MAX_REGIONS; i++) {
        uint64_t region_pages = info->regions[i].size >> PMM_PAGE_SHIFT;
        uint64_t bitmap_size = (region_pages + 63) / 64 * sizeof(uint64_t);
        total_bitmap_size += bitmap_size;
    }
    
    // Initialize boot allocator after kernel, boot page tables, AND relocated FDT
    uint64_t boot_pt_start = (kernel_end_phys + BOOT_PAGE_TABLE_PADDING) & ~(PMM_PAGE_SIZE - 1);
    uint64_t boot_pt_end = boot_pt_start + BOOT_PAGE_TABLE_SIZE;
    
    // Get actual FDT location and size
    extern void *fdt_mgr_get_phys_addr(void);
    extern size_t fdt_mgr_get_size(void);
    void *fdt_phys = fdt_mgr_get_phys_addr();
    size_t fdt_size = fdt_mgr_get_size();
    
    // Start bootstrap allocator after boot page tables
    uint64_t pmm_bootstrap_start = boot_pt_end;
    
    // If FDT exists and is after our current position, move past it
    if (fdt_phys && fdt_size) {
        uint64_t fdt_start = (uint64_t)fdt_phys;
        uint64_t fdt_end = fdt_start + fdt_size;
        
        if (fdt_end > pmm_bootstrap_start && fdt_start < (pmm_bootstrap_start + 64 * 1024)) {
            pmm_bootstrap_start = (fdt_end + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
        }
    }
    
    // Align to page boundary
    pmm_bootstrap_start = (pmm_bootstrap_start + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
    
    // Boot allocator needs to be large enough for all bitmaps plus some overhead
    uint64_t pmm_bootstrap_size = total_bitmap_size + 8192; // Extra space for alignment
    pmm_bootstrap_size = (pmm_bootstrap_size + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
    
    uint64_t pmm_bootstrap_end = pmm_bootstrap_start + pmm_bootstrap_size;
    
    uart_puts("\nPMM: Initializing multi-region memory manager\n");
    uart_puts("  Total regions: ");
    uart_puthex(info->count);
    uart_puts("\n  Total RAM: ");
    uart_puthex(info->total_size / 1024 / 1024);
    uart_puts(" MB\n");
    uart_puts("  Total bitmap size needed: ");
    uart_puthex(total_bitmap_size);
    uart_puts(" bytes\n");
    
    // Safety check for kernel physical base
    extern uint64_t kernel_phys_base;
    if (pmm_bootstrap_end > kernel_phys_base + (32 * 1024 * 1024)) {
        uart_puts("ERROR: Boot allocator would extend beyond 32MB from kernel!\n");
        return;
    }
    
    pmm_bootstrap_init(pmm_bootstrap_start, pmm_bootstrap_end);
    
    // Initialize each memory region
    pmm_region_count = 0;
    for (int i = 0; i < info->count && i < PMM_MAX_REGIONS; i++) {
        pmm_region_t *region = &pmm_regions[pmm_region_count];
        
        region->base = info->regions[i].base;
        region->size = info->regions[i].size;
        region->total_pages = region->size >> PMM_PAGE_SHIFT;
        region->free_pages = region->total_pages; // Initially all free
        region->bitmap_size = (region->total_pages + 63) / 64 * sizeof(uint64_t);
        
        // Allocate bitmap for this region
        region->bitmap = (uint64_t*)pmm_bootstrap_alloc(region->bitmap_size, 8);
        if (!region->bitmap) {
            uart_puts("ERROR: Failed to allocate bitmap for region ");
            uart_puthex(i);
            uart_puts("\n");
            continue;
        }
        
        // Clear bitmap (all pages initially free)
        for (size_t j = 0; j < region->bitmap_size / sizeof(uint64_t); j++) {
            region->bitmap[j] = 0;
        }
        
        // Link regions
        if (pmm_region_count > 0) {
            pmm_regions[pmm_region_count - 1].next = region;
        }
        region->next = NULL;
        
        // Update global statistics
        pmm_stats.total_pages += region->total_pages;
        pmm_stats.free_pages += region->free_pages;
        
        uart_puts("  Region ");
        uart_puthex(pmm_region_count);
        uart_puts(": base=");
        uart_puthex(region->base);
        uart_puts(" size=");
        uart_puthex(region->size / 1024 / 1024);
        uart_puts(" MB, pages=");
        uart_puthex(region->total_pages);
        uart_puts("\n");
        
        pmm_region_count++;
    }
    
    // Now reserve system areas in each region they overlap with
    // Reserve kernel memory
    pmm_reserve_region(info->regions[0].base, kernel_end_phys - info->regions[0].base, "Kernel");
    
    // Reserve boot page tables
    pmm_reserve_region(boot_pt_start, BOOT_PAGE_TABLE_SIZE, "Boot Page Tables");
    
    // Reserve bootstrap allocator region (includes PMM bitmaps)
    pmm_reserve_region(pmm_bootstrap_start, pmm_bootstrap_used(), "PMM Bootstrap");
    
    // Reserve FDT region if it exists
    if (fdt_phys && fdt_size) {
        pmm_reserve_region((uint64_t)fdt_phys, fdt_size, "FDT");
    }
    
    // Reserve UART region
    pmm_reserve_region(0x09000000, 0x1000, "PL011 UART");
    
    // Mark PMM as initialized
    pmm_initialized = true;
    
    uart_puts("PMM: Initialization complete\n");
    uart_puts("  Total pages: ");
    uart_puthex(pmm_stats.total_pages);
    uart_puts(" (");
    uart_puthex(pmm_stats.total_pages * PMM_PAGE_SIZE / 1024 / 1024);
    uart_puts(" MB)\n");
    uart_puts("  Free pages: ");
    uart_puthex(pmm_stats.free_pages);
    uart_puts(" (");
    uart_puthex(pmm_stats.free_pages * PMM_PAGE_SIZE / 1024 / 1024);
    uart_puts(" MB)\n");
}

// Allocate a single page
uint64_t pmm_alloc_page(void) {
    // Try each region in order
    for (int i = 0; i < pmm_region_count; i++) {
        pmm_region_t *region = &pmm_regions[i];
        
        // Find first free page in this region
        for (uint64_t page = 0; page < region->total_pages; page++) {
            if (!pmm_test_bit(region, page)) {
                pmm_set_bit(region, page);
                region->free_pages--;
                pmm_stats.free_pages--;
                pmm_stats.allocated_pages++;
                
                uint64_t pa = page_to_addr(region, page);
                
                // Clear the page - use DMAP if available, otherwise use identity mapping
                uint64_t va;
                if (vmm_is_dmap_ready()) {
                    va = PHYS_TO_DMAP(pa);
                } else {
                    // Use identity mapping (physical address directly)
                    va = pa;
                }
                
                uint64_t* page_ptr = (uint64_t*)va;
                for (size_t j = 0; j < PMM_PAGE_SIZE / sizeof(uint64_t); j++) {
                    page_ptr[j] = 0;
                }
                
                return pa;
            }
        }
    }
    
    return 0;  // Out of memory
}

// Allocate a single page for page table use
uint64_t pmm_alloc_page_table(void) {
    uint64_t pa = pmm_alloc_page();
    if (pa) {
        pmm_stats.page_table_pages++;
    }
    return pa;
}

// Allocate multiple contiguous pages
uint64_t pmm_alloc_pages(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page();
    
    // Try each region in order
    for (int i = 0; i < pmm_region_count; i++) {
        pmm_region_t *region = &pmm_regions[i];
        
        // Find contiguous free pages
        uint64_t start = 0;
        uint64_t found = 0;
        
        for (uint64_t page = 0; page < region->total_pages; page++) {
            if (!pmm_test_bit(region, page)) {
                if (found == 0) start = page;
                found++;
                
                // Check if we have enough pages
                if (found == count) {
                    // Check if they don't exceed bounds
                    if ((start + count) <= region->total_pages) {
                        // Mark all pages as allocated
                        for (uint64_t j = start; j < start + count; j++) {
                            pmm_set_bit(region, j);
                        }
                        region->free_pages -= count;
                        pmm_stats.free_pages -= count;
                        pmm_stats.allocated_pages += count;
                        
                        uint64_t pa = page_to_addr(region, start);
                        
                        // Clear the pages
                        uint64_t va;
                        if (vmm_is_dmap_ready()) {
                            va = PHYS_TO_DMAP(pa);
                        } else {
                            va = pa;
                        }
                        
                        uint64_t* page_ptr = (uint64_t*)va;
                        for (size_t j = 0; j < (PMM_PAGE_SIZE * count) / sizeof(uint64_t); j++) {
                            page_ptr[j] = 0;
                        }
                        
                        return pa;
                    } else {
                        // Boundary check failed, reset search
                        found = 0;
                    }
                }
            } else {
                found = 0;
            }
        }
    }
    
    return 0;  // Allocation failed
}

// Free a page
void pmm_free_page(uint64_t pa) {
    pmm_region_t *region = pmm_find_region(pa);
    if (!region) {
        return;  // Invalid address
    }
    
    uint64_t page = addr_to_page(region, pa);
    if (page >= region->total_pages) {
        return;  // Page out of bounds
    }
    
    if (!pmm_test_bit(region, page)) {
        return;  // Already free
    }
    
    pmm_clear_bit(region, page);
    region->free_pages++;
    pmm_stats.free_pages++;
    pmm_stats.allocated_pages--;
}

// Free multiple contiguous pages
void pmm_free_pages(uint64_t pa, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(pa + i * PMM_PAGE_SIZE);
    }
}

// Mark region as reserved
void pmm_reserve_region(uint64_t base, uint64_t size, const char* name) {
    (void)name; // Name is now tracked by memmap module
    
    uint64_t end = base + size;
    
    // Check each memory region for overlap
    for (int i = 0; i < pmm_region_count; i++) {
        pmm_region_t *region = &pmm_regions[i];
        uint64_t region_end = region->base + region->size;
        
        // Check for overlap
        if (end <= region->base || base >= region_end) {
            continue;  // No overlap
        }
        
        // Calculate overlap bounds
        uint64_t overlap_start = base > region->base ? base : region->base;
        uint64_t overlap_end = end < region_end ? end : region_end;
        
        // Mark overlapping pages as used
        uint64_t start_page = addr_to_page(region, overlap_start);
        uint64_t end_page = addr_to_page(region, overlap_end + PMM_PAGE_SIZE - 1);
        
        // Ensure we don't go past region bounds
        if (end_page > region->total_pages) {
            end_page = region->total_pages;
        }
        
        for (uint64_t page = start_page; page < end_page; page++) {
            if (!pmm_test_bit(region, page)) {
                pmm_set_bit(region, page);
                region->free_pages--;
                pmm_stats.free_pages--;
                pmm_stats.reserved_pages++;
            }
        }
    }
}

// Reserve a single page
void pmm_reserve_page(uint64_t pa) {
    pmm_region_t *region = pmm_find_region(pa);
    if (!region) {
        return;  // Invalid address
    }
    
    // Align to page boundary
    pa = pa & ~(PMM_PAGE_SIZE - 1);
    
    uint64_t page = addr_to_page(region, pa);
    if (page < region->total_pages && !pmm_test_bit(region, page)) {
        pmm_set_bit(region, page);
        region->free_pages--;
        pmm_stats.free_pages--;
        pmm_stats.reserved_pages++;
    }
}

// Get memory statistics
void pmm_get_stats(pmm_stats_t* stats) {
    if (stats) {
        *stats = pmm_stats;
    }
}

// Check if address is available
int pmm_is_available(uint64_t pa) {
    pmm_region_t *region = pmm_find_region(pa);
    if (!region) {
        return 0;  // Not in any managed region
    }
    
    uint64_t page = addr_to_page(region, pa);
    return page < region->total_pages && !pmm_test_bit(region, page);
}

// Get start of managed physical memory (lowest address)
uint64_t pmm_get_memory_start(void) {
    if (pmm_region_count == 0) {
        return 0;
    }
    
    uint64_t start = pmm_regions[0].base;
    for (int i = 1; i < pmm_region_count; i++) {
        if (pmm_regions[i].base < start) {
            start = pmm_regions[i].base;
        }
    }
    return start;
}

// Get end of managed physical memory (highest address)
uint64_t pmm_get_memory_end(void) {
    if (pmm_region_count == 0) {
        return 0;
    }
    
    uint64_t end = pmm_regions[0].base + pmm_regions[0].size;
    for (int i = 1; i < pmm_region_count; i++) {
        uint64_t region_end = pmm_regions[i].base + pmm_regions[i].size;
        if (region_end > end) {
            end = region_end;
        }
    }
    return end;
}

// Print memory statistics
void pmm_print_stats(void) {
    uart_puts("\nPhysical Memory Manager Statistics:\n");
    uart_puts("===================================\n");
    
    uart_puts("Memory Regions: ");
    uart_puthex(pmm_region_count);
    uart_puts("\n");
    
    for (int i = 0; i < pmm_region_count; i++) {
        pmm_region_t *region = &pmm_regions[i];
        uart_puts("  Region ");
        uart_puthex(i);
        uart_puts(": ");
        uart_puthex(region->base);
        uart_puts(" - ");
        uart_puthex(region->base + region->size);
        uart_puts(" (");
        uart_puthex(region->size / 1024 / 1024);
        uart_puts(" MB)\n");
        uart_puts("    Free: ");
        uart_puthex(region->free_pages);
        uart_puts(" pages (");
        uart_puthex(region->free_pages * PMM_PAGE_SIZE / 1024 / 1024);
        uart_puts(" MB)\n");
    }
    
    uart_puts("\nTotal Statistics:\n");
    uart_puts("Total Pages: ");
    uart_puthex(pmm_stats.total_pages);
    uart_puts(" (");
    uart_puthex(pmm_stats.total_pages * PMM_PAGE_SIZE / 1024 / 1024);
    uart_puts(" MB)\n");
    
    uart_puts("Free Pages:  ");
    uart_puthex(pmm_stats.free_pages);
    uart_puts(" (");
    uart_puthex(pmm_stats.free_pages * PMM_PAGE_SIZE / 1024 / 1024);
    uart_puts(" MB)\n");
    
    uart_puts("Used Pages:  ");
    uart_puthex(pmm_stats.allocated_pages + pmm_stats.reserved_pages);
    uart_puts(" (");
    uart_puthex((pmm_stats.allocated_pages + pmm_stats.reserved_pages) * PMM_PAGE_SIZE / 1024 / 1024);
    uart_puts(" MB)\n");
    
    uart_puts("\nBitmap Usage:\n");
    uint64_t total_bitmap = 0;
    for (int i = 0; i < pmm_region_count; i++) {
        total_bitmap += pmm_regions[i].bitmap_size;
    }
    uart_puts("Total bitmap size: ");
    uart_puthex(total_bitmap);
    uart_puts(" bytes\n");
    
    uart_puts("\nReserved Regions:\n");
    uart_puts("-----------------\n");
    
    // Show kernel reservation
    extern uint64_t kernel_phys_base;
    extern char _kernel_end;
    uart_puts("Kernel:           ");
    uart_puthex(kernel_phys_base);
    uart_puts(" - ");
    uart_puthex(VIRT_TO_PHYS((uint64_t)&_kernel_end));
    uart_puts("\n");
    
    // Show boot page tables
    uint64_t kernel_end_phys = VIRT_TO_PHYS((uint64_t)&_kernel_end);
    uint64_t boot_pt_start = (kernel_end_phys + BOOT_PAGE_TABLE_PADDING) & ~(PMM_PAGE_SIZE - 1);
    uart_puts("Boot Page Tables: ");
    uart_puthex(boot_pt_start);
    uart_puts(" - ");
    uart_puthex(boot_pt_start + BOOT_PAGE_TABLE_SIZE);
    uart_puts("\n");
    
    // Show bootstrap allocator region
    uart_puts("PMM Bootstrap:    ");
    uart_puthex(boot_pt_start + BOOT_PAGE_TABLE_SIZE);
    uart_puts(" - ");
    uart_puthex((uint64_t)pmm_bootstrap_current());
    uart_puts(" (");
    uart_puthex(pmm_bootstrap_used());
    uart_puts(" bytes used)\n");
    
    // Show FDT reservation if available
    extern void *fdt_mgr_get_phys_addr(void);
    extern size_t fdt_mgr_get_size(void);
    void *fdt_phys = fdt_mgr_get_phys_addr();
    size_t fdt_size = fdt_mgr_get_size();
    if (fdt_phys && fdt_size) {
        uart_puts("FDT:              ");
        uart_puthex((uint64_t)fdt_phys);
        uart_puts(" - ");
        uart_puthex((uint64_t)fdt_phys + fdt_size);
        uart_puts(" (");
        uart_puthex(fdt_size);
        uart_puts(" bytes)\n");
    }
}

// Check if PMM is initialized
bool pmm_is_initialized(void) {
    return pmm_initialized;
}