#include <tests/mmu_tests.h>
#include <memory/mmu.h>
#include <memory/paging.h>
#include <uart.h>

// Minimal MMU tests - just check critical functionality
void run_all_mmu_tests(void) {
    uart_puts("\n=== MMU Tests ===\n");
    
    // Test 1: 4KB page mapping
    uart_puts("4KB page mapping ... ");
    uint64_t test_va = 0x60000000;
    uint64_t test_pa = 0x48000000;  // Use memory within our 256MB range
    map_page(test_va, test_pa, PTE_KERNEL_PAGE);
    
    // Write and read back
    uint64_t* ptr = (uint64_t*)test_va;
    *ptr = 0x1234567890ABCDEF;
    if (*ptr == 0x1234567890ABCDEF) {
        uart_puts("PASS\n");
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 2: 2MB block mapping
    uart_puts("2MB block mapping ... ");
    test_va = 0x62000000;
    test_pa = 0x49000000;  // Use memory within our 256MB range
    map_range(test_va, test_pa, 0x200000, PTE_KERNEL_BLOCK);
    
    ptr = (uint64_t*)test_va;
    *ptr = 0xFEDCBA0987654321;
    if (*ptr == 0xFEDCBA0987654321) {
        uart_puts("PASS\n");
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 3: TLB invalidation
    uart_puts("TLB invalidation ... ");
    test_va = 0x64000000;
    map_page(test_va, 0x4A000000, PTE_KERNEL_PAGE);  // Use valid PA
    ptr = (uint64_t*)test_va;
    *ptr = 0x1111111111111111;
    
    // Remap to different physical page
    map_page(test_va, 0x4B000000, PTE_KERNEL_PAGE);  // Use different valid PA
    tlb_invalidate_page(test_va);
    
    // Should read zeros from new page
    if (*ptr == 0) {
        uart_puts("PASS\n");
    } else {
        uart_puts("FAIL\n");
    }
}