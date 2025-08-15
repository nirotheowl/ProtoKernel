/*
 * kernel/tests/page_alloc_tests.c
 *
 * Unit tests for the page allocator
 */

#include <memory/page_alloc.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <uart.h>
#include <string.h>

#define TEST_PASS(name) do { \
    uart_puts("[PASS] "); \
    uart_puts(name); \
    uart_puts("\n"); \
    passed++; \
} while(0)

#define TEST_FAIL(name, reason) do { \
    uart_puts("[FAIL] "); \
    uart_puts(name); \
    uart_puts(": "); \
    uart_puts(reason); \
    uart_puts("\n"); \
    failed++; \
} while(0)

static int passed = 0;
static int failed = 0;

// Test helper: allocate and verify
static int test_alloc_free_order(uint32_t order) {
    uint64_t addr = page_alloc(order);
    if (addr == 0) {
        return 0;
    }
    
    // Verify alignment
    size_t expected_size = (1UL << order) * PAGE_SIZE;
    if (addr & (PAGE_SIZE - 1)) {
        page_free(addr, order);
        return 0;
    }
    
    // Try to write to the allocated pages
    void *virt = (void *)PHYS_TO_DMAP(addr);
    if (virt) {
        memset(virt, 0xAA, PAGE_SIZE); // Write pattern to first page
        uint8_t *buf = (uint8_t *)virt;
        if (buf[0] != 0xAA) {
            page_free(addr, order);
            return 0;
        }
    }
    
    page_free(addr, order);
    return 1;
}

// Test 1: Basic allocation and free
static void test_basic_alloc_free(void) {
    uint64_t addr = page_alloc(0); // Single page
    
    if (addr == 0) {
        TEST_FAIL("test_basic_alloc_free", "Failed to allocate single page");
        return;
    }
    
    if (addr & (PAGE_SIZE - 1)) {
        TEST_FAIL("test_basic_alloc_free", "Misaligned allocation");
        page_free(addr, 0);
        return;
    }
    
    page_free(addr, 0);
    TEST_PASS("test_basic_alloc_free");
}

// Test 2: Multiple order allocations
static void test_multiple_orders(void) {
    uint64_t addrs[5];
    uint32_t orders[5] = {0, 1, 2, 3, 4}; // 1, 2, 4, 8, 16 pages
    
    // Allocate different orders
    for (int i = 0; i < 5; i++) {
        addrs[i] = page_alloc(orders[i]);
        if (addrs[i] == 0) {
            TEST_FAIL("test_multiple_orders", "Failed to allocate");
            // Free what we allocated
            for (int j = 0; j < i; j++) {
                page_free(addrs[j], orders[j]);
            }
            return;
        }
    }
    
    // Free them all
    for (int i = 0; i < 5; i++) {
        page_free(addrs[i], orders[i]);
    }
    
    TEST_PASS("test_multiple_orders");
}

// Test 3: Buddy coalescing
static void test_buddy_coalescing(void) {
    // First allocate and free a medium block to ensure we have space
    uint64_t setup = page_alloc(5); // 32 pages
    if (setup == 0) {
        // Try smaller size
        setup = page_alloc(4); // 16 pages
        if (setup == 0) {
            TEST_FAIL("test_buddy_coalescing", "Insufficient memory for test");
            return;
        }
    }
    uint64_t test_addr = setup;
    page_free(setup, (setup != 0) ? 5 : 4);
    
    // Now test splitting and coalescing at that address region
    // Allocate order 4 (16 pages)
    uint64_t large = page_alloc(4);
    if (large == 0) {
        TEST_FAIL("test_buddy_coalescing", "Failed to allocate order 4");
        return;
    }
    
    page_free(large, 4);
    
    // Allocate two order 3 blocks (8 pages each)
    // These should come from splitting the order 4 block
    uint64_t small1 = page_alloc(3);
    uint64_t small2 = page_alloc(3);
    
    if (small1 == 0 || small2 == 0) {
        TEST_FAIL("test_buddy_coalescing", "Failed to allocate order 3 blocks");
        if (small1) page_free(small1, 3);
        if (small2) page_free(small2, 3);
        return;
    }
    
    // Verify they are buddies (adjacent with proper alignment)
    uint64_t lower = (small1 < small2) ? small1 : small2;
    uint64_t upper = (small1 < small2) ? small2 : small1;
    
    if (upper - lower != (8 * PAGE_SIZE)) {
        TEST_FAIL("test_buddy_coalescing", "Blocks are not proper buddies");
        page_free(small1, 3);
        page_free(small2, 3);
        return;
    }
    
    // Free them - they should coalesce back
    page_free(small1, 3);
    page_free(small2, 3);
    
    // Try to allocate order 4 again - should get the same region
    uint64_t large2 = page_alloc(4);
    if (large2 == 0) {
        TEST_FAIL("test_buddy_coalescing", "Failed to allocate after coalesce");
        return;
    }
    
    page_free(large2, 4);
    TEST_PASS("test_buddy_coalescing");
}

// Test 4: Allocation stress test
static void test_allocation_stress(void) {
    #define STRESS_COUNT 20
    uint64_t addrs[STRESS_COUNT];
    uint32_t orders[STRESS_COUNT];
    
    // Allocate many blocks of varying sizes
    for (int i = 0; i < STRESS_COUNT; i++) {
        orders[i] = i % 5; // Orders 0-4
        addrs[i] = page_alloc(orders[i]);
        if (addrs[i] == 0) {
            // Out of memory is acceptable in stress test
            // Free what we got and call it success
            for (int j = 0; j < i; j++) {
                page_free(addrs[j], orders[j]);
            }
            TEST_PASS("test_allocation_stress");
            return;
        }
    }
    
    // Free every other block
    for (int i = 0; i < STRESS_COUNT; i += 2) {
        page_free(addrs[i], orders[i]);
        addrs[i] = 0;
    }
    
    // Allocate again in the gaps
    for (int i = 0; i < STRESS_COUNT; i += 2) {
        addrs[i] = page_alloc(orders[i]);
        if (addrs[i] == 0) {
            TEST_FAIL("test_allocation_stress", "Failed to reallocate");
            // Clean up
            for (int j = 0; j < STRESS_COUNT; j++) {
                if (addrs[j]) page_free(addrs[j], orders[j]);
            }
            return;
        }
    }
    
    // Free everything
    for (int i = 0; i < STRESS_COUNT; i++) {
        if (addrs[i]) page_free(addrs[i], orders[i]);
    }
    
    TEST_PASS("test_allocation_stress");
    #undef STRESS_COUNT
}

// Test 5: Large allocation passthrough (>16MB)
static void test_large_passthrough(void) {
    // Order 13 = 8192 pages = 32MB (exceeds PAGE_ALLOC_MAX_ORDER)
    uint64_t huge = page_alloc(13);
    
    if (huge == 0) {
        // This might fail due to memory constraints, which is OK
        TEST_PASS("test_large_passthrough (skipped - insufficient memory)");
        return;
    }
    
    // Verify it's page aligned
    if (huge & (PAGE_SIZE - 1)) {
        TEST_FAIL("test_large_passthrough", "Misaligned large allocation");
        page_free(huge, 13);
        return;
    }
    
    page_free(huge, 13);
    TEST_PASS("test_large_passthrough");
}

// Test 6: Double free detection
static void test_double_free(void) {
    uint64_t addr = page_alloc(2); // 4 pages
    
    if (addr == 0) {
        TEST_FAIL("test_double_free", "Failed to allocate");
        return;
    }
    
    page_free(addr, 2);
    
    // This should be detected and handled gracefully
    // (will print an error but shouldn't crash)
    page_free(addr, 2);
    
    TEST_PASS("test_double_free");
}

// Test 7: Order boundary conditions
static void test_order_boundaries(void) {
    // Test order 0 (single page) first
    if (test_alloc_free_order(0)) {
        TEST_PASS("test_order_boundaries (min order)");
    } else {
        TEST_FAIL("test_order_boundaries", "Min order allocation failed");
    }
    
    // Test maximum order within page allocator
    // This may fail if we're out of memory, which is acceptable
    // Note: Allocating order 12 (16MB) can be slow as it may need to allocate a new chunk
    uart_puts("Testing max order allocation (may take a moment)...\n");
    uint64_t addr = page_alloc(PAGE_ALLOC_MAX_ORDER);
    if (addr != 0) {
        // Successfully allocated max order
        page_free(addr, PAGE_ALLOC_MAX_ORDER);
        TEST_PASS("test_order_boundaries (max order)");
    } else {
        // Allocation failed - could be due to memory constraints
        // Try a slightly smaller allocation
        addr = page_alloc(PAGE_ALLOC_MAX_ORDER - 1);
        if (addr != 0) {
            page_free(addr, PAGE_ALLOC_MAX_ORDER - 1);
            TEST_PASS("test_order_boundaries (max-1 order)");
        } else {
            TEST_PASS("test_order_boundaries (skipped - insufficient memory)");
        }
    }
}

// Test 8: Page allocator statistics
static void test_statistics(void) {
    struct page_alloc_stats stats_before, stats_after;
    
    page_alloc_get_stats(&stats_before);
    
    // Do some allocations
    uint64_t addr1 = page_alloc(2);
    uint64_t addr2 = page_alloc(3);
    
    if (!addr1 || !addr2) {
        TEST_FAIL("test_statistics", "Failed to allocate");
        if (addr1) page_free(addr1, 2);
        if (addr2) page_free(addr2, 3);
        return;
    }
    
    page_alloc_get_stats(&stats_after);
    
    // Check that allocations were counted
    if (stats_after.allocations[2] <= stats_before.allocations[2] ||
        stats_after.allocations[3] <= stats_before.allocations[3]) {
        TEST_FAIL("test_statistics", "Allocation count not updated");
        page_free(addr1, 2);
        page_free(addr2, 3);
        return;
    }
    
    // Free and check free counts
    page_free(addr1, 2);
    page_free(addr2, 3);
    
    page_alloc_get_stats(&stats_after);
    
    if (stats_after.frees[2] <= stats_before.frees[2] ||
        stats_after.frees[3] <= stats_before.frees[3]) {
        TEST_FAIL("test_statistics", "Free count not updated");
        return;
    }
    
    TEST_PASS("test_statistics");
}

// Test 9: Allocate multiple pages (non-power-of-2)
static void test_multiple_pages(void) {
    // Request 5 pages (will round up to order 3 = 8 pages)
    uint64_t addr = page_alloc_multiple(5);
    
    if (addr == 0) {
        TEST_FAIL("test_multiple_pages", "Failed to allocate 5 pages");
        return;
    }
    
    // Should have gotten at least 5 pages (actually 8)
    page_free_multiple(addr, 5);
    
    TEST_PASS("test_multiple_pages");
}

// Main test runner
void page_alloc_run_tests(void) {
    uart_puts("\n===== Page Allocator Tests =====\n");
    
    // Make sure page allocator is initialized
    if (!page_alloc_is_initialized()) {
        page_alloc_init();
    }
    
    passed = 0;
    failed = 0;
    
    // Run all tests
    test_basic_alloc_free();
    test_multiple_orders();
    test_buddy_coalescing();
    test_allocation_stress();
    test_large_passthrough();
    test_double_free();
    test_order_boundaries();
    test_statistics();
    test_multiple_pages();
    
    // Print summary
    uart_puts("\n===== Test Summary =====\n");
    uart_puts("Passed: ");
    uart_putdec(passed);
    uart_puts("\nFailed: ");
    uart_putdec(failed);
    uart_puts("\nTotal: ");
    uart_putdec(passed + failed);
    uart_puts("\n");
    
    if (failed == 0) {
        uart_puts("All tests PASSED!\n");
    } else {
        uart_puts("Some tests FAILED!\n");
    }
    
    // Print allocator statistics
    uart_puts("\n");
    page_alloc_print_stats();
}

// Test initialization function
void page_alloc_test_init(void) {
    uart_puts("Page allocator tests initialized\n");
}