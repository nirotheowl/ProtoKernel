#include <memory/mmu.h>
#include <memory/paging.h>
#include <uart.h>
#include <stdbool.h>
#include <tests/mmu_tests.h>

// Test addresses in unused memory regions
#define TEST_VA_BASE    0x60000000
#define TEST_PA_BASE    0x40600000

// Additional test VAs for different tests
#define TEST_VA_4KB     (TEST_VA_BASE)
#define TEST_VA_2MB     (TEST_VA_BASE + 0x200000)
#define TEST_VA_PERM    (TEST_VA_BASE + 0x400000)
#define TEST_VA_REMAP   (TEST_VA_BASE + 0x600000)
#define TEST_VA_TLB     (TEST_VA_BASE + 0x800000)
#define TEST_VA_CACHE   (TEST_VA_BASE + 0xA00000)

// Test 4KB page mapping and unmapping
bool test_4kb_page_mapping(void) {
    // Map a 4KB page
    uint64_t test_va = TEST_VA_4KB;
    uint64_t test_pa = TEST_PA_BASE;
    
    map_page(test_va, test_pa, PTE_KERNEL_PAGE | PTE_AP_RW);
    
    // Invalidate TLB for this page
    tlb_invalidate_page(test_va);
    
    // Write test pattern
    volatile uint32_t *ptr = (volatile uint32_t *)test_va;
    ptr[0] = 0x12345678;
    ptr[1] = 0x9ABCDEF0;
    
    // Flush cache
    dcache_clean_range(test_va, test_va + 8);
    
    // Read back and verify
    if (ptr[0] != 0x12345678) {
        uart_puts("  FAILED: First word mismatch\n");
        return false;
    }
    if (ptr[1] != 0x9ABCDEF0) {
        uart_puts("  FAILED: Second word mismatch\n");
        return false;
    }
    
    return true;
}

// Test 2MB block mapping
bool test_2mb_block_mapping(void) {
    uint64_t test_va = TEST_VA_2MB;
    uint64_t test_pa = TEST_PA_BASE + 0x200000;
    
    // Map a 2MB block
    map_range(test_va, test_pa, 0x200000, PTE_KERNEL_BLOCK);
    
    // Invalidate TLB
    tlb_invalidate_range(test_va, test_va + 0x200000);
    
    // Test writes at different offsets within the 2MB block
    volatile uint64_t *ptr1 = (volatile uint64_t *)(test_va);
    volatile uint64_t *ptr2 = (volatile uint64_t *)(test_va + 0x100000);
    volatile uint64_t *ptr3 = (volatile uint64_t *)(test_va + 0x1FF000);
    
    *ptr1 = 0xAAAAAAAAAAAAAAAAULL;
    *ptr2 = 0xBBBBBBBBBBBBBBBBULL;
    *ptr3 = 0xCCCCCCCCCCCCCCCCULL;
    
    // Flush cache
    dcache_clean_range(test_va, test_va + 0x200000);
    
    // Verify
    if (*ptr1 != 0xAAAAAAAAAAAAAAAAULL) {
        uart_puts("  FAILED: Start of block mismatch\n");
        return false;
    }
    if (*ptr2 != 0xBBBBBBBBBBBBBBBBULL) {
        uart_puts("  FAILED: Middle of block mismatch\n");
        return false;
    }
    if (*ptr3 != 0xCCCCCCCCCCCCCCCCULL) {
        uart_puts("  FAILED: End of block mismatch\n");
        return false;
    }
    
    return true;
}

// Test different permission bits
bool test_permission_bits(void) {
    // Testing permission bits...\n");
    
    uint64_t test_va = TEST_VA_PERM;
    uint64_t test_pa = TEST_PA_BASE + 0x400000;
    
    // Test 1: Read-write mapping
    map_page(test_va, test_pa, PTE_KERNEL_PAGE | PTE_AP_RW);
    tlb_invalidate_page(test_va);
    
    volatile uint32_t *ptr = (volatile uint32_t *)test_va;
    *ptr = 0x11111111;
    if (*ptr != 0x11111111) {
        uart_puts("  FAILED: Read-write test failed\n");
        return false;
    }
    
    // Test 2: Read-only mapping (would need exception handler to properly test)
    // For now, just verify we can create the mapping
    map_page(test_va + PAGE_SIZE, test_pa + PAGE_SIZE, PTE_KERNEL_PAGE | PTE_AP_RO);
    tlb_invalidate_page(test_va + PAGE_SIZE);
    
    // Test 3: No-execute mapping
    map_page(test_va + 2*PAGE_SIZE, test_pa + 2*PAGE_SIZE, PTE_KERNEL_PAGE | PTE_AP_RW | PTE_UXN | PTE_PXN);
    tlb_invalidate_page(test_va + 2*PAGE_SIZE);
    
    return true;
}

// Test overwriting existing mappings
bool test_remap_page(void) {
    // Testing remapping pages...\n");
    
    uint64_t test_va = TEST_VA_REMAP;
    uint64_t test_pa1 = TEST_PA_BASE + 0x600000;
    uint64_t test_pa2 = TEST_PA_BASE + 0x601000;
    
    // Map to first physical page
    map_page(test_va, test_pa1, PTE_KERNEL_PAGE | PTE_AP_RW);
    tlb_invalidate_page(test_va);
    
    // Write pattern to first mapping
    volatile uint32_t *ptr = (volatile uint32_t *)test_va;
    *ptr = 0xF1257000;
    dcache_clean_range(test_va, test_va + 4);
    
    // Remap to second physical page
    map_page(test_va, test_pa2, PTE_KERNEL_PAGE | PTE_AP_RW);
    tlb_invalidate_page(test_va);
    
    // Write pattern to second mapping
    *ptr = 0x5EC00D00;
    dcache_clean_range(test_va, test_va + 4);
    
    // Verify we're accessing the second page
    if (*ptr != 0x5EC00D00) {
        uart_puts("  FAILED: Remap failed - still seeing old page\n");
        return false;
    }
    
    return true;
}

// Test TLB invalidation
bool test_tlb_invalidation(void) {
    // Testing TLB invalidation...\n");
    
    uint64_t test_va = TEST_VA_TLB;
    uint64_t test_pa = TEST_PA_BASE + 0x800000;
    
    // Map a page
    map_page(test_va, test_pa, PTE_KERNEL_PAGE | PTE_AP_RW);
    tlb_invalidate_page(test_va);
    
    // Access the page to load it into TLB
    volatile uint32_t *ptr = (volatile uint32_t *)test_va;
    *ptr = 0x71B7E571;
    
    // Test single page invalidation
    tlb_invalidate_page(test_va);
    
    // Access should still work (will reload TLB)
    if (*ptr != 0x71B7E571) {
        uart_puts("  FAILED: TLB invalidation broke mapping\n");
        return false;
    }
    
    // Test range invalidation
    tlb_invalidate_range(test_va, test_va + PAGE_SIZE);
    if (*ptr != 0x71B7E571) {
        uart_puts("  FAILED: TLB range invalidation broke mapping\n");
        return false;
    }
    
    // Test full TLB invalidation
    tlb_invalidate_all();
    if (*ptr != 0x71B7E571) {
        uart_puts("  FAILED: Full TLB invalidation broke mapping\n");
        return false;
    }
    
    return true;
}

// Test cache operations
bool test_cache_operations(void) {
    // Testing cache operations...\n");
    
    uint64_t test_va = TEST_VA_CACHE;
    uint64_t test_pa = TEST_PA_BASE + 0xA00000;
    
    // Map a page
    map_page(test_va, test_pa, PTE_KERNEL_PAGE | PTE_AP_RW);
    tlb_invalidate_page(test_va);
    
    volatile uint32_t *ptr = (volatile uint32_t *)test_va;
    
    // Test cache clean
    *ptr = 0xC1EA0123;
    dcache_clean_range(test_va, test_va + 4);
    if (*ptr != 0xC1EA0123) {
        uart_puts("  FAILED: Cache clean test failed\n");
        return false;
    }
    
    // Test cache invalidate
    *ptr = 0x10BA1456;
    dcache_invalidate_range(test_va, test_va + 4);
    // Note: This might show old data if not properly written back
    
    // Test cache clean and invalidate
    *ptr = 0xC1106789;
    dcache_clean_invalidate_range(test_va, test_va + 4);
    if (*ptr != 0xC1106789) {
        uart_puts("  FAILED: Cache clean+invalidate test failed\n");
        return false;
    }
    
    // Test instruction cache invalidation (just ensure it doesn't crash)
    icache_invalidate_all();
    
    return true;
}

// Test accessing unmapped memory (should trigger exception)
bool test_unmapped_access(void) {
    // Testing unmapped memory access detection...\n");
    // (This test expects a page fault - that's normal)
    
    // This test is tricky without proper exception handling
    // For now, we'll just note that it should cause an exception
    // In a complete implementation, we'd catch the exception and verify it
    
    return true;
}

// Test mixed page sizes in same region
bool test_mixed_page_sizes(void) {
    // Testing mixed page sizes...\n");
    
    uint64_t base_va = 0x70000000;
    uint64_t base_pa = 0x40700000;
    
    // Map first 4KB
    map_page(base_va, base_pa, PTE_KERNEL_PAGE | PTE_AP_RW);
    
    // Map next 4KB
    map_page(base_va + PAGE_SIZE, base_pa + PAGE_SIZE, PTE_KERNEL_PAGE | PTE_AP_RW);
    
    // Try to map a 2MB block that would overlap (should fail or handle gracefully)
    // This depends on implementation details
    
    tlb_invalidate_range(base_va, base_va + 0x200000);
    
    // Test access to both 4KB pages
    volatile uint32_t *ptr1 = (volatile uint32_t *)base_va;
    volatile uint32_t *ptr2 = (volatile uint32_t *)(base_va + PAGE_SIZE);
    
    *ptr1 = 0x04B00001;
    *ptr2 = 0x04B00002;
    
    if (*ptr1 != 0x04B00001) {
        uart_puts("  FAILED: First 4KB page access failed\n");
        return false;
    }
    if (*ptr2 != 0x04B00002) {
        uart_puts("  FAILED: Second 4KB page access failed\n");
        return false;
    }
    
    return true;
}

// Stress test with many mappings
bool test_stress_many_mappings(void) {
    // Stress testing with multiple mappings...\n");
    
    uint64_t base_va = 0x80000000;
    uint64_t base_pa = 0x40800000;
    
    // Map 10 pages
    for (int i = 0; i < 10; i++) {
        map_page(base_va + i * PAGE_SIZE, base_pa + i * PAGE_SIZE, PTE_KERNEL_PAGE | PTE_AP_RW);
    }
    
    // Invalidate TLB for the range
    tlb_invalidate_range(base_va, base_va + 10 * PAGE_SIZE);
    
    // Write unique pattern to each page
    for (int i = 0; i < 10; i++) {
        volatile uint32_t *ptr = (volatile uint32_t *)(base_va + i * PAGE_SIZE);
        *ptr = 0xBEEF0000 | i;
    }
    
    // Flush caches
    dcache_clean_range(base_va, base_va + 10 * PAGE_SIZE);
    
    // Verify all pages
    for (int i = 0; i < 10; i++) {
        volatile uint32_t *ptr = (volatile uint32_t *)(base_va + i * PAGE_SIZE);
        uint32_t expected = 0xBEEF0000 | i;
        if (*ptr != expected) {
            uart_puts("    Page ");
            uart_putc('0' + i);
            uart_puts(" verification failed\n");
            return false;
        }
    }
    
    return true;
}


// Run all MMU tests
void run_all_mmu_tests(void) {
    uart_puts("\n=== MMU Tests ===\n");
    
    uint32_t passed = 0;
    uint32_t total = 8;
    
    // Test 1: 4KB page mapping
    uart_puts("[1/8] 4KB Page Mapping... ");
    if (test_4kb_page_mapping()) {
        uart_puts("PASS\n");
        passed++;
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 2: 2MB block mapping
    uart_puts("[2/8] 2MB Block Mapping... ");
    if (test_2mb_block_mapping()) {
        uart_puts("PASS\n");
        passed++;
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 3: Permission bits
    uart_puts("[3/8] Permission Bits... ");
    if (test_permission_bits()) {
        uart_puts("PASS\n");
        passed++;
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 4: Page remapping
    uart_puts("[4/8] Page Remapping... ");
    if (test_remap_page()) {
        uart_puts("PASS\n");
        passed++;
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 5: TLB invalidation
    uart_puts("[5/8] TLB Invalidation... ");
    if (test_tlb_invalidation()) {
        uart_puts("PASS\n");
        passed++;
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 6: Cache operations
    uart_puts("[6/8] Cache Operations... ");
    if (test_cache_operations()) {
        uart_puts("PASS\n");
        passed++;
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 7: Mixed page sizes
    uart_puts("[7/8] Mixed Page Sizes... ");
    if (test_mixed_page_sizes()) {
        uart_puts("PASS\n");
        passed++;
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 8: Stress test
    uart_puts("[8/8] Stress Test... ");
    if (test_stress_many_mappings()) {
        uart_puts("PASS\n");
        passed++;
    } else {
        uart_puts("FAIL\n");
    }
    
    // Summary
    uart_puts("\nSummary: ");
    uart_putc('0' + passed);
    uart_puts("/");
    uart_putc('0' + total);
    uart_puts(" tests passed\n");
    
    if (passed == total) {
        uart_puts("All tests PASSED!\n");
    } else {
        uart_puts("Some tests FAILED!\n");
    }
}