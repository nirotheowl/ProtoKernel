#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <memory/vmm.h>
#include <uart.h>
#include <stdint.h>
#include <boot_config.h>

// External symbols from linker script
extern char _kernel_end;

// Bitmap to track page allocation (1 bit per page)
// For 1GB of memory with 4KB pages, needs 32KB for the bitmap
static uint64_t pmm_bitmap[PMM_MAX_PAGES / 64];  // 64 bits per uint64_t

// Memory statistics
static pmm_stats_t pmm_stats = {0};

// Start of managed memory (after kernel)
static uint64_t pmm_start = 0;
static uint64_t pmm_end = 0;


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
void pmm_init(uint64_t kernel_end, uint64_t mem_size) {
    // kernel_end is a virtual address, convert to physical
    uint64_t kernel_end_phys = VIRT_TO_PHYS(kernel_end);
    
    // Align kernel_end to page boundary
    pmm_start = (kernel_end_phys + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
    pmm_end = 0x40000000 + mem_size;  // Assuming RAM starts at 0x40000000
    
    // Calculate total pages
    pmm_stats.total_pages = (pmm_end - pmm_start) >> PMM_PAGE_SHIFT;
    pmm_stats.free_pages = pmm_stats.total_pages;
    
    // Clear bitmap (all pages initially free)
    for (size_t i = 0; i < PMM_MAX_PAGES / 64; i++) {
        pmm_bitmap[i] = 0;
    }
    
    // Reserve kernel memory (0x40000000 to kernel_end_phys)
    pmm_reserve_region(0x40000000, kernel_end_phys - 0x40000000, "Kernel");
    
    // Reserve boot page tables (allocated by boot.S after kernel_end)
    // boot.S: add x26, x26, #BOOT_PAGE_TABLE_PADDING then align to page
    uint64_t boot_pt_start = (kernel_end_phys + BOOT_PAGE_TABLE_PADDING) & ~(PMM_PAGE_SIZE - 1);
    pmm_reserve_region(boot_pt_start, BOOT_PAGE_TABLE_SIZE, "Boot Page Tables");
    
    // Reserve the PMM bitmap itself
    uint64_t bitmap_size = sizeof(pmm_bitmap);
    uint64_t bitmap_vaddr = (uint64_t)pmm_bitmap;
    uint64_t bitmap_paddr = VIRT_TO_PHYS(bitmap_vaddr);
    pmm_reserve_region(bitmap_paddr, bitmap_size, "PMM Bitmap");
    
    // Reserve UART region
    pmm_reserve_region(0x09000000, 0x1000, "PL011 UART");
    
    // Print initialization status (disabled until UART is available)
    // uart_puts("\nPhysical Memory Manager initialized:\n");
    // uart_puts("  Start: ");
    // uart_puthex(pmm_start);
    // uart_puts("\n  End: ");
    // uart_puthex(pmm_end);
    // uart_puts("\n  Total pages: ");
    // uart_puthex(pmm_stats.total_pages);
    // uart_puts("\n  Free pages: ");
    // uart_puthex(pmm_stats.free_pages);
    // uart_puts("\n  Page size: ");
    // uart_puthex(PMM_PAGE_SIZE);
    // uart_puts("\n");
}

// Allocate a single page
uint64_t pmm_alloc_page(void) {
    // Find first free page
    for (uint64_t i = 0; i < pmm_stats.total_pages; i++) {
        if (!pmm_test_bit(i)) {
            pmm_set_bit(i);
            pmm_stats.free_pages--;
            pmm_stats.allocated_pages++;
            
            uint64_t pa = page_to_addr(i);
            
            // Clear the page - use DMAP if available, otherwise use identity mapping
            uint64_t va;
            if (vmm_is_dmap_ready()) {
                va = PHYS_TO_DMAP(pa);
                // uart_puts("  PMM: Using DMAP to clear page at PA ");
            } else {
                // Use identity mapping (physical address directly)
                va = pa;
                // uart_puts("  PMM: Using identity mapping to clear page at PA ");
            }
            // uart_puthex(pa);
            // uart_puts(" VA ");
            // uart_puthex(va);
            // uart_puts("\n");
            
            uint64_t* page_ptr = (uint64_t*)va;
            // uart_puts("  PMM: About to clear page...\n");
            for (size_t j = 0; j < PMM_PAGE_SIZE / sizeof(uint64_t); j++) {
                page_ptr[j] = 0;
            }
            // uart_puts("  PMM: Page cleared successfully\n");
            
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
    
    for (uint64_t i = 0; i < pmm_stats.total_pages; i++) {
        if (!pmm_test_bit(i)) {
            if (found == 0) start = i;
            found++;
            
            if (found == count) {
                // Mark all pages as allocated
                for (uint64_t j = start; j < start + count; j++) {
                    pmm_set_bit(j);
                }
                pmm_stats.free_pages -= count;
                pmm_stats.allocated_pages += count;
                
                uint64_t pa = page_to_addr(start);
                
                // Clear the pages - use DMAP if available, otherwise use identity mapping
                uint64_t va;
                if (vmm_is_dmap_ready()) {
                    va = PHYS_TO_DMAP(pa);
                } else {
                    // Use identity mapping (physical address directly)
                    va = pa;
                }
                uint64_t* page_ptr = (uint64_t*)va;
                for (size_t j = 0; j < (PMM_PAGE_SIZE * count) / sizeof(uint64_t); j++) {
                    page_ptr[j] = 0;
                }
                
                return pa;
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
    
    for (uint64_t i = start_page; i < end_page; i++) {
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
    if (!pmm_test_bit(page)) {
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
    return !pmm_test_bit(page);
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
    
    uart_puts("\nReserved Regions:\n");
    uart_puts("-----------------\n");
    
    // Show kernel reservation
    uart_puts("Kernel:           0x40000000 - ");
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
