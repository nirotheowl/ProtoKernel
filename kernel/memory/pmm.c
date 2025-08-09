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

// Dynamic bitmap pointer (allocated at runtime)
static uint64_t *pmm_bitmap = NULL;
static size_t pmm_bitmap_size = 0;
static size_t pmm_total_pages = 0;

// Memory statistics
static pmm_stats_t pmm_stats = {0};

// Start and end of managed memory
static uint64_t pmm_start = 0;
static uint64_t pmm_end = 0;

// Initialization flag
static bool pmm_initialized = false;


// Helper functions
static void pmm_set_bit(uint64_t page_num) {
    pmm_bitmap[page_num / 64] |= (1ULL << (page_num % 64));
}

static void pmm_clear_bit(uint64_t page_num) {
    pmm_bitmap[page_num / 64] &= ~(1ULL << (page_num % 64));
}

static int pmm_test_bit(uint64_t page_num) {
    return (pmm_bitmap[page_num / 64] & (1ULL << (page_num % 64))) != 0;
}

static uint64_t addr_to_page(uint64_t addr) {
    return (addr - pmm_start) >> PMM_PAGE_SHIFT;
}

static uint64_t page_to_addr(uint64_t page) {
    return pmm_start + (page << PMM_PAGE_SHIFT);
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
    
    // For now, we'll use the first memory region
    // TODO: Handle multiple memory regions
    uint64_t mem_base = info->regions[0].base;
    uint64_t mem_size = info->regions[0].size;
    
    // Initialize boot allocator after kernel, boot page tables, AND relocated FDT
    // Add padding for boot page tables as done in original code
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
        
        // Check if FDT overlaps with our planned boot allocator region
        if (fdt_end > pmm_bootstrap_start && fdt_start < (pmm_bootstrap_start + 64 * 1024)) {
            // Move boot allocator after FDT with some padding
            pmm_bootstrap_start = (fdt_end + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
            uart_puts("PMM: Adjusting boot allocator to avoid FDT at ");
            uart_puthex(fdt_start);
            uart_puts("\n");
        }
    }
    
    // Align to page boundary
    pmm_bootstrap_start = (pmm_bootstrap_start + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
    
    // Calculate exact space needed for PMM bitmap
    // PMM will manage all memory from mem_base to mem_base + mem_size
    uint64_t total_pages_to_manage = mem_size >> PMM_PAGE_SHIFT;
    uint64_t bitmap_size_needed = (total_pages_to_manage + 63) / 64 * sizeof(uint64_t);
    
    // Boot allocator needs to be large enough for the bitmap plus some overhead
    // Add 4KB for alignment and allocation metadata
    uint64_t pmm_bootstrap_size = bitmap_size_needed + 4096;
    
    // Round up to page boundary
    pmm_bootstrap_size = (pmm_bootstrap_size + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
    
    uint64_t pmm_bootstrap_end = pmm_bootstrap_start + pmm_bootstrap_size;
    
    uart_puts("PMM: Configuring bootstrap allocator for ");
    uart_puthex(mem_size / 1024 / 1024);
    uart_puts(" MB RAM\n");
    uart_puts("  Bitmap needs: ");
    uart_puthex(bitmap_size_needed);
    uart_puts(" bytes (");
    uart_puthex(bitmap_size_needed / 1024);
    uart_puts(" KB)\n");
    uart_puts("  Boot allocator size: ");
    uart_puthex(pmm_bootstrap_size);
    uart_puts(" bytes\n");
    
    // Safety check: ensure boot allocator doesn't extend too far
    if (pmm_bootstrap_end > mem_base + (16 * 1024 * 1024)) {
        uart_puts("ERROR: Boot allocator would extend beyond 16MB from RAM base!\n");
        uart_puts("  Boot alloc end: ");
        uart_puthex(pmm_bootstrap_end);
        uart_puts("\n");
        return;
    }
    
    pmm_bootstrap_init(pmm_bootstrap_start, pmm_bootstrap_end);
    
    // Set up PMM range (manage all memory from base)
    pmm_start = mem_base;
    pmm_end = mem_base + mem_size;
    
    // Calculate total pages and bitmap size
    pmm_total_pages = (pmm_end - pmm_start) >> PMM_PAGE_SHIFT;
    pmm_bitmap_size = (pmm_total_pages + 63) / 64 * sizeof(uint64_t); // Round up
    
    uart_puts("\nPMM: Initializing with dynamic allocation\n");
    uart_puts("  Memory range: ");
    uart_puthex(mem_base);
    uart_puts(" - ");
    uart_puthex(mem_base + mem_size);
    uart_puts(" (");
    uart_puthex(mem_size / 1024 / 1024);
    uart_puts(" MB)\n");
    uart_puts("  Total pages: ");
    uart_puthex(pmm_total_pages);
    uart_puts("\n  Bitmap size: ");
    uart_puthex(pmm_bitmap_size);
    uart_puts(" bytes\n");
    
    // Allocate bitmap using boot allocator
    pmm_bitmap = (uint64_t*)pmm_bootstrap_alloc(pmm_bitmap_size, 8);
    if (!pmm_bitmap) {
        uart_puts("ERROR: Failed to allocate PMM bitmap\n");
        return;
    }
    
    uart_puts("  Bitmap allocated at: ");
    uart_puthex((uint64_t)pmm_bitmap);
    uart_puts("\n");
    
    // Initialize statistics
    pmm_stats.total_pages = pmm_total_pages;
    pmm_stats.free_pages = pmm_total_pages;
    
    // Clear bitmap (all pages initially free)
    for (size_t i = 0; i < pmm_bitmap_size / sizeof(uint64_t); i++) {
        pmm_bitmap[i] = 0;
    }
    
    // Reserve kernel memory (mem_base to kernel_end_phys)
    pmm_reserve_region(mem_base, kernel_end_phys - mem_base, "Kernel");
    
    // Reserve boot page tables
    pmm_reserve_region(boot_pt_start, BOOT_PAGE_TABLE_SIZE, "Boot Page Tables");
    
    // Reserve bootstrap allocator region (includes PMM bitmap)
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
    uart_puts("  Free pages: ");
    uart_puthex(pmm_stats.free_pages);
    uart_puts(" (");
    uart_puthex(pmm_stats.free_pages * PMM_PAGE_SIZE / 1024 / 1024);
    uart_puts(" MB)\n");
}

// Allocate a single page
uint64_t pmm_alloc_page(void) {
    // Find first free page
    for (uint64_t i = 0; i < pmm_total_pages; i++) {
        if (!pmm_test_bit(i)) {
            pmm_set_bit(i);
            pmm_stats.free_pages--;
            pmm_stats.allocated_pages++;
            
            uint64_t pa = page_to_addr(i);
            
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
    
    
    // Find contiguous free pages
    uint64_t start = 0;
    uint64_t found = 0;
    
    for (uint64_t i = 0; i < pmm_total_pages; i++) {
        if (!pmm_test_bit(i)) {
            if (found == 0) start = i;
            found++;
            
            // Check if we have enough pages
            if (found == count) {
                // Check if they don't exceed bounds
                if ((start + count) <= pmm_total_pages) {
                    // Mark all pages as allocated
                for (uint64_t j = start; j < start + count; j++) {
                    pmm_set_bit(j);
                }
                pmm_stats.free_pages -= count;
                pmm_stats.allocated_pages += count;
                
                uint64_t pa = page_to_addr(start);
                
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
    
    return 0;  // Allocation failed
}

// Free a page
void pmm_free_page(uint64_t pa) {
    if (pa < pmm_start || pa >= pmm_end) {
        return;  // Invalid address
    }
    
    uint64_t page = addr_to_page(pa);
    if (!pmm_test_bit(page)) {
        return;  // Already free
    }
    
    pmm_clear_bit(page);
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
    
    // If region overlaps with managed memory, mark pages as used
    uint64_t start = base;
    uint64_t end = base + size;
    
    if (end <= pmm_start || start >= pmm_end) {
        return;  // No overlap
    }
    
    // Adjust to managed range
    if (start < pmm_start) start = pmm_start;
    if (end > pmm_end) end = pmm_end;
    
    // Mark pages as used
    uint64_t start_page = addr_to_page(start);
    uint64_t end_page = addr_to_page(end + PMM_PAGE_SIZE - 1);
    
    for (uint64_t i = start_page; i < end_page && i < pmm_total_pages; i++) {
        if (!pmm_test_bit(i)) {
            pmm_set_bit(i);
            pmm_stats.free_pages--;
            pmm_stats.reserved_pages++;
        }
    }
}

// Reserve a single page
void pmm_reserve_page(uint64_t pa) {
    // Check if the page is within managed memory
    if (pa < pmm_start || pa >= pmm_end) {
        return;  // Outside managed range
    }
    
    // Align to page boundary
    pa = pa & ~(PMM_PAGE_SIZE - 1);
    
    uint64_t page = addr_to_page(pa);
    if (page < pmm_total_pages && !pmm_test_bit(page)) {
        pmm_set_bit(page);
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
    if (pa < pmm_start || pa >= pmm_end) {
        return 0;
    }
    
    uint64_t page = addr_to_page(pa);
    return page < pmm_total_pages && !pmm_test_bit(page);
}

// Get start of managed physical memory
uint64_t pmm_get_memory_start(void) {
    return pmm_start;
}

// Get end of managed physical memory
uint64_t pmm_get_memory_end(void) {
    return pmm_end;
}

// Print memory statistics
void pmm_print_stats(void) {
    uart_puts("\nPhysical Memory Manager Statistics:\n");
    uart_puts("===================================\n");
    
    uart_puts("Memory Range: ");
    uart_puthex(pmm_start);
    uart_puts(" - ");
    uart_puthex(pmm_end);
    uart_puts("\n");
    
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
    uart_puthex(pmm_stats.allocated_pages);
    uart_puts(" (");
    uart_puthex(pmm_stats.allocated_pages * PMM_PAGE_SIZE / 1024 / 1024);
    uart_puts(" MB)\n");
    
    uart_puts("\nBitmap Info:\n");
    uart_puts("Address: ");
    uart_puthex((uint64_t)pmm_bitmap);
    uart_puts("\nSize: ");
    uart_puthex(pmm_bitmap_size);
    uart_puts(" bytes\n");
    
    uart_puts("\nReserved Regions:\n");
    uart_puts("-----------------\n");
    
    // Show kernel reservation (use dynamic kernel_phys_base)
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