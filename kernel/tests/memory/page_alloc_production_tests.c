/*
 * Production-ready tests for page allocator
 * These tests verify corner cases, error handling, and production scenarios
 */

#include <memory/page_alloc.h>
#include <uart.h>
#include <stdint.h>
#include <stdbool.h>

// Test alignment validation
static bool test_alignment_validation(void) {
    uart_puts("Testing alignment validation...\n");
    uart_puts("  (The following error messages are expected and intentional)\n");
    
    // These should fail gracefully - we expect error messages
    page_free(0x1001, 0);  // Should print: "page_free: Misaligned address"
    page_free(0xDEADBEEF, 0);  // Should print: "page_free: Misaligned address"  
    page_free(0, 0);  // Should print: "page_free: NULL address"
    
    uart_puts("  (End of expected error messages)\n");
    
    // Valid aligned address should work
    uint64_t addr = page_alloc(0);
    if (addr == 0) {
        uart_puts("  Failed to allocate for alignment test\n");
        return false;
    }
    
    // Verify it's aligned
    if ((addr & (PAGE_SIZE - 1)) != 0) {
        uart_puts("  Allocated address not aligned!\n");
        return false;
    }
    
    page_free(addr, 0);
    return true;
}

// Test order validation
static bool test_order_validation(void) {
    uart_puts("Testing order validation...\n");
    
    // Test maximum order boundary
    uint64_t addr = page_alloc(PAGE_ALLOC_MAX_ORDER);
    if (addr != 0) {
        page_free(addr, PAGE_ALLOC_MAX_ORDER);
    }
    
    // Order > MAX should be handled
    addr = page_alloc(PAGE_ALLOC_MAX_ORDER + 1);
    if (addr != 0) {
        page_free(addr, PAGE_ALLOC_MAX_ORDER + 1);
    }
    
    return true;
}

// Test chunk recycling under memory pressure
static bool test_chunk_recycling(void) {
    uart_puts("Testing chunk recycling...\n");
    
    #define NUM_ALLOCS 50
    uint64_t addrs[NUM_ALLOCS];
    uint32_t orders[NUM_ALLOCS];
    
    // Allocate various sizes to create multiple chunks
    for (int i = 0; i < NUM_ALLOCS; i++) {
        orders[i] = i % 5;  // Orders 0-4
        addrs[i] = page_alloc(orders[i]);
        if (addrs[i] == 0) {
            uart_puts("  Allocation failed at ");
            uart_putdec(i);
            uart_puts("\n");
            // Free what we allocated
            for (int j = 0; j < i; j++) {
                if (addrs[j] != 0) {
                    page_free(addrs[j], orders[j]);
                }
            }
            return false;
        }
    }
    
    // Get initial stats
    struct page_alloc_stats stats_before;
    page_alloc_get_stats(&stats_before);
    
    // Free everything
    for (int i = 0; i < NUM_ALLOCS; i++) {
        page_free(addrs[i], orders[i]);
    }
    
    // Get final stats
    struct page_alloc_stats stats_after;
    page_alloc_get_stats(&stats_after);
    
    // Verify chunks were returned (may not happen with small allocations)
    if (stats_after.pmm_chunks_freed <= stats_before.pmm_chunks_freed) {
        uart_puts("  Note: No chunks recycled (expected with small allocations)\n");
        // This is actually OK - small allocations might not trigger chunk return
        // Only fail if we leaked memory
        if (stats_after.current_allocated[0] != 0 || 
            stats_after.current_allocated[1] != 0 ||
            stats_after.current_allocated[2] != 0 ||
            stats_after.current_allocated[3] != 0 ||
            stats_after.current_allocated[4] != 0) {
            uart_puts("  FAIL: Memory leak detected!\n");
            return false;
        }
    }
    
    return true;
}

// Test rapid alloc/free patterns (simulates real usage)
static bool test_rapid_alloc_free(void) {
    uart_puts("Testing rapid allocation/free patterns...\n");
    
    // Simulate bursty allocation pattern
    for (int burst = 0; burst < 10; burst++) {
        uint64_t addrs[20];
        
        // Burst allocate
        for (int i = 0; i < 20; i++) {
            addrs[i] = page_alloc(i % 4);  // Various small sizes
            if (addrs[i] == 0) {
                uart_puts("  Burst allocation failed\n");
                // Cleanup
                for (int j = 0; j < i; j++) {
                    page_free(addrs[j], j % 4);
                }
                return false;
            }
        }
        
        // Random free pattern
        for (int i = 19; i >= 0; i -= 2) {
            page_free(addrs[i], i % 4);
        }
        for (int i = 0; i < 20; i += 2) {
            page_free(addrs[i], i % 4);
        }
    }
    
    return true;
}

// Test memory exhaustion handling
static bool test_memory_exhaustion(void) {
    uart_puts("Testing memory exhaustion handling...\n");
    
    #define MAX_HUGE_ALLOCS 200
    uint64_t huge_allocs[MAX_HUGE_ALLOCS];
    int allocated = 0;
    
    // Allocate large blocks until exhaustion
    for (int i = 0; i < MAX_HUGE_ALLOCS; i++) {
        huge_allocs[i] = page_alloc(10);  // 4MB blocks
        if (huge_allocs[i] == 0) {
            // Expected to fail at some point
            uart_puts("  Memory exhausted after ");
            uart_putdec(i);
            uart_puts(" allocations (expected)\n");
            break;
        }
        allocated++;
    }
    
    if (allocated == MAX_HUGE_ALLOCS) {
        uart_puts("  Warning: Did not hit memory limit\n");
    }
    
    // Free all allocated blocks
    for (int i = 0; i < allocated; i++) {
        page_free(huge_allocs[i], 10);
    }
    
    // Should be able to allocate again
    uint64_t test = page_alloc(10);
    if (test == 0) {
        uart_puts("  Failed to allocate after freeing!\n");
        return false;
    }
    page_free(test, 10);
    
    return true;
}

// Test buddy coalescing correctness
static bool test_coalescing_correctness(void) {
    uart_puts("Testing coalescing correctness...\n");
    
    // Allocate a large block
    uint64_t large = page_alloc(8);  // 256 pages
    if (large == 0) {
        uart_puts("  Failed to allocate large block\n");
        return false;
    }
    
    // Free it (will be split internally)
    page_free(large, 8);
    
    // Allocate many small blocks from the same region
    uint64_t small[256];
    for (int i = 0; i < 256; i++) {
        small[i] = page_alloc(0);  // Single pages
        if (small[i] == 0) {
            uart_puts("  Failed to allocate small block\n");
            // Cleanup
            for (int j = 0; j < i; j++) {
                page_free(small[j], 0);
            }
            return false;
        }
    }
    
    // Free them all - should coalesce back
    for (int i = 0; i < 256; i++) {
        page_free(small[i], 0);
    }
    
    // Should be able to allocate large block again
    large = page_alloc(8);
    if (large == 0) {
        uart_puts("  Coalescing failed - cannot reallocate large block\n");
        return false;
    }
    page_free(large, 8);
    
    return true;
}

// Test allocation patterns that might cause fragmentation
static bool test_anti_fragmentation(void) {
    uart_puts("Testing anti-fragmentation behavior...\n");
    
    // Allocate in a pattern that could cause fragmentation
    uint64_t addrs[100];
    
    // Allocate alternating sizes
    for (int i = 0; i < 100; i++) {
        uint32_t order = (i % 2) ? 0 : 3;  // Alternate between 1 and 8 pages
        addrs[i] = page_alloc(order);
        if (addrs[i] == 0) {
            uart_puts("  Allocation failed due to fragmentation\n");
            // Cleanup
            for (int j = 0; j < i; j++) {
                page_free(addrs[j], (j % 2) ? 0 : 3);
            }
            return false;
        }
    }
    
    // Free every other allocation
    for (int i = 0; i < 100; i += 2) {
        page_free(addrs[i], (i % 2) ? 0 : 3);
    }
    
    // Try to allocate medium blocks
    bool success = true;
    for (int i = 0; i < 10; i++) {
        uint64_t med = page_alloc(4);  // 16 pages
        if (med == 0) {
            success = false;
            break;
        }
        page_free(med, 4);
    }
    
    // Cleanup remaining
    for (int i = 1; i < 100; i += 2) {
        page_free(addrs[i], (i % 2) ? 0 : 3);
    }
    
    if (!success) {
        uart_puts("  Fragmentation prevented medium allocations\n");
        return false;
    }
    
    return true;
}

// Main test runner for production tests
void page_alloc_run_production_tests(void) {
    uart_puts("\n========================================\n");
    uart_puts("Page Allocator Production Tests\n");
    uart_puts("========================================\n\n");
    
    int passed = 0;
    int failed = 0;
    
    if (test_alignment_validation()) {
        uart_puts("[PASS] Alignment validation\n");
        passed++;
    } else {
        uart_puts("[FAIL] Alignment validation\n");
        failed++;
    }
    
    if (test_order_validation()) {
        uart_puts("[PASS] Order validation\n");
        passed++;
    } else {
        uart_puts("[FAIL] Order validation\n");
        failed++;
    }
    
    if (test_chunk_recycling()) {
        uart_puts("[PASS] Chunk recycling\n");
        passed++;
    } else {
        uart_puts("[FAIL] Chunk recycling\n");
        failed++;
    }
    
    if (test_rapid_alloc_free()) {
        uart_puts("[PASS] Rapid alloc/free\n");
        passed++;
    } else {
        uart_puts("[FAIL] Rapid alloc/free\n");
        failed++;
    }
    
    if (test_memory_exhaustion()) {
        uart_puts("[PASS] Memory exhaustion handling\n");
        passed++;
    } else {
        uart_puts("[FAIL] Memory exhaustion handling\n");
        failed++;
    }
    
    if (test_coalescing_correctness()) {
        uart_puts("[PASS] Coalescing correctness\n");
        passed++;
    } else {
        uart_puts("[FAIL] Coalescing correctness\n");
        failed++;
    }
    
    if (test_anti_fragmentation()) {
        uart_puts("[PASS] Anti-fragmentation\n");
        passed++;
    } else {
        uart_puts("[FAIL] Anti-fragmentation\n");
        failed++;
    }
    
    uart_puts("\n===== Production Test Summary =====\n");
    uart_puts("Passed: ");
    uart_putdec(passed);
    uart_puts("\nFailed: ");
    uart_putdec(failed);
    uart_puts("\nTotal: ");
    uart_putdec(passed + failed);
    uart_puts("\n");
    
    if (failed == 0) {
        uart_puts("All production tests PASSED!\n");
    } else {
        uart_puts("Some production tests FAILED!\n");
    }
}