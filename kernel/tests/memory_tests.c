#include <tests/memory_tests.h>
#include <memory/pmm.h>
#include <memory/paging.h>
#include <memory/memmap.h>
#include <memory/mmu.h>
#include <uart.h>

// Minimal edge case tests - only critical checks
void run_comprehensive_memory_tests(void) {
    uart_puts("\n=== Memory Edge Cases ===\n");
    
    // Test 1: Large contiguous allocation
    uart_puts("Large contiguous alloc ... ");
    uint64_t large_alloc = pmm_alloc_pages(256);  // 1MB
    if (large_alloc != 0) {
        // Verify pages are contiguous
        int ok = 1;
        for (int i = 0; i < 256; i++) {
            uint64_t* ptr = (uint64_t*)(large_alloc + i * PMM_PAGE_SIZE);
            *ptr = i;
            if (*ptr != i) ok = 0;
        }
        pmm_free_pages(large_alloc, 256);
        uart_puts(ok ? "PASS\n" : "FAIL\n");
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 2: Page table consistency
    uart_puts("Page table consistency ... ");
    uint64_t test_va = 0xE0000000;
    uint64_t test_pa = 0x4C000000;  // Use valid PA within our RAM
    map_page(test_va, test_pa, PTE_KERNEL_PAGE);
    
    // Verify page table walk
    pgd_t* pgd = get_kernel_pgd();
    uint32_t pgd_idx = PGDIR_INDEX(test_va);
    if ((pgd[pgd_idx] & PTE_VALID) &&
        ((uint64_t*)(test_va))[0] == ((uint64_t*)(test_pa))[0]) {
        uart_puts("PASS\n");
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 3: Memory barriers
    uart_puts("Memory barriers ... ");
    uint64_t test_page = pmm_alloc_page();
    if (test_page) {
        volatile uint64_t* ptr = (volatile uint64_t*)test_page;
        *ptr = 0xAAAAAAAAAAAAAAAAULL;
        dsb(sy);
        *ptr = 0x5555555555555555ULL;
        dmb(sy);
        isb();
        
        if (*ptr == 0x5555555555555555ULL) {
            uart_puts("PASS\n");
        } else {
            uart_puts("FAIL\n");
        }
        pmm_free_page(test_page);
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 4: Device memory attributes
    uart_puts("Device memory attrs ... ");
    uint64_t device_attrs = memmap_get_page_attrs(0x09000000);  // UART
    uint64_t kernel_attrs = memmap_get_page_attrs(0x40200000);  // Kernel
    
    // Check MAIR index bits (4:2)
    uint64_t device_mair = (device_attrs >> 2) & 0x7;
    uint64_t kernel_mair = (kernel_attrs >> 2) & 0x7;
    
    if (device_mair == MT_DEVICE_nGnRnE && kernel_mair == MT_NORMAL) {
        uart_puts("PASS\n");
    } else {
        uart_puts("FAIL\n");
    }
}