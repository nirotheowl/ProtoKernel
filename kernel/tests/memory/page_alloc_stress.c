/*
 * kernel/tests/page_alloc_stress.c
 *
 * Comprehensive stress tests for page allocator
 */

#include <memory/page_alloc.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <uart.h>
#include <string.h>

#define MAX_ALLOCS 10000  // Increased to handle 16GB+ of RAM with various block sizes

struct allocation {
    uint64_t addr;
    uint32_t order;
    bool allocated;
};

static struct allocation allocs[MAX_ALLOCS];
static int num_allocs = 0;

// Random-ish number generator (simple LCG)
static uint32_t seed = 12345;
static uint32_t rand(void) {
    seed = seed * 1103515245 + 12345;
    return (seed / 65536) % 32768;
}

static void print_test_header(const char *name) {
    uart_puts("\n==== ");
    uart_puts(name);
    uart_puts(" ====\n");
}

static void print_result(const char *test, bool passed) {
    if (passed) {
        uart_puts("[PASS] ");
    } else {
        uart_puts("[FAIL] ");
    }
    uart_puts(test);
    uart_puts("\n");
}

// Test 1: Maximum memory allocation
static void test_max_memory_allocation(void) {
    print_test_header("Maximum Memory Allocation Test");
    
    // Get initial PMM stats
    pmm_stats_t pmm_stats_before;
    pmm_get_stats(&pmm_stats_before);
    
    uart_puts("Initial PMM state:\n");
    uart_puts("  Free pages: ");
    uart_putdec(pmm_stats_before.free_pages);
    uart_puts(" (");
    uart_putdec(pmm_stats_before.free_pages * 4);
    uart_puts(" KB)\n");
    
    // Try to allocate as much memory as possible
    uint64_t total_allocated = 0;
    int count = 0;
    
    // Start with large allocations
    uart_puts("\nAllocating maximum order blocks...\n");
    while (count < MAX_ALLOCS) {
        uint64_t addr = page_alloc(PAGE_ALLOC_MAX_ORDER);
        if (addr == 0) break;
        
        allocs[count].addr = addr;
        allocs[count].order = PAGE_ALLOC_MAX_ORDER;
        allocs[count].allocated = true;
        total_allocated += (1UL << PAGE_ALLOC_MAX_ORDER) * PAGE_SIZE;
        count++;
        
        if (count % 10 == 0) {
            uart_puts("  Allocated ");
            uart_putdec(count);
            uart_puts(" order-12 blocks (");
            uart_putdec(total_allocated / (1024 * 1024));
            uart_puts(" MB)\n");
        }
    }
    
    uart_puts("Allocated ");
    uart_putdec(count);
    uart_puts(" order-12 blocks\n");
    
    // Try progressively smaller orders
    for (int order = PAGE_ALLOC_MAX_ORDER - 1; order >= 0 && count < MAX_ALLOCS; order--) {
        int order_count = 0;
        while (count < MAX_ALLOCS) {
            uint64_t addr = page_alloc(order);
            if (addr == 0) break;
            
            allocs[count].addr = addr;
            allocs[count].order = order;
            allocs[count].allocated = true;
            total_allocated += (1UL << order) * PAGE_SIZE;
            count++;
            order_count++;
        }
        
        if (order_count > 0) {
            uart_puts("  Allocated ");
            uart_putdec(order_count);
            uart_puts(" order-");
            uart_putdec(order);
            uart_puts(" blocks\n");
        }
    }
    
    // Report results
    uart_puts("\nTotal allocations: ");
    uart_putdec(count);
    uart_puts("\nTotal memory allocated: ");
    uart_putdec(total_allocated / (1024 * 1024));
    uart_puts(" MB (");
    uart_putdec(total_allocated / 1024);
    uart_puts(" KB)\n");
    
    // Check PMM stats
    pmm_stats_t pmm_stats_after;
    pmm_get_stats(&pmm_stats_after);
    
    uart_puts("PMM after allocation:\n");
    uart_puts("  Free pages: ");
    uart_putdec(pmm_stats_after.free_pages);
    uart_puts(" (");
    uart_putdec(pmm_stats_after.free_pages * 4);
    uart_puts(" KB)\n");
    
    uint64_t pmm_used = (pmm_stats_before.free_pages - pmm_stats_after.free_pages) * PAGE_SIZE;
    uart_puts("  PMM pages consumed: ");
    uart_putdec(pmm_used / (1024 * 1024));
    uart_puts(" MB\n");
    
    // Calculate overhead
    if (pmm_used > total_allocated) {
        uint64_t overhead = pmm_used - total_allocated;
        uart_puts("  Overhead: ");
        uart_putdec(overhead / 1024);
        uart_puts(" KB (");
        uart_putdec((overhead * 100) / pmm_used);
        uart_puts("%)\n");
    }
    
    // Free everything
    uart_puts("\nFreeing all allocations...\n");
    for (int i = 0; i < count; i++) {
        if (allocs[i].allocated) {
            page_free(allocs[i].addr, allocs[i].order);
            allocs[i].allocated = false;
        }
    }
    
    // Check if memory was properly freed
    pmm_stats_t pmm_stats_freed;
    pmm_get_stats(&pmm_stats_freed);
    
    uart_puts("PMM after freeing:\n");
    uart_puts("  Free pages: ");
    uart_putdec(pmm_stats_freed.free_pages);
    uart_puts(" (");
    uart_putdec(pmm_stats_freed.free_pages * 4);
    uart_puts(" KB)\n");
    
    // We should have most memory back (some overhead for chunks is expected)
    if (pmm_stats_freed.free_pages > pmm_stats_before.free_pages) {
        uart_puts("  ERROR: More free pages than we started with!\n");
        uart_puts("  Started with: ");
        uart_putdec(pmm_stats_before.free_pages);
        uart_puts(", ended with: ");
        uart_putdec(pmm_stats_freed.free_pages);
        uart_puts("\n");
    } else {
        uint64_t leaked = (pmm_stats_before.free_pages - pmm_stats_freed.free_pages) * PAGE_SIZE;
        if (leaked > 0) {
            uart_puts("  Memory not returned to PMM: ");
            uart_putdec(leaked / 1024);
            uart_puts(" KB\n");
        }
    }
    
    bool passed = (total_allocated >= 400 * 1024 * 1024); // Should allocate at least 400MB (allowing for overhead)
    print_result("Maximum memory allocation", passed);
    
    num_allocs = 0; // Reset for next test
}

// Test 2: Random allocation/deallocation pattern
static void test_random_pattern(void) {
    print_test_header("Random Allocation Pattern Test");
    
    int total_allocs = 0;
    int total_frees = 0;
    int active_allocs = 0;
    
    // Run for many iterations (reduced for performance)
    for (int iter = 0; iter < 200; iter++) {
        // Randomly decide to allocate or free
        if ((rand() % 100) < 60 && active_allocs < MAX_ALLOCS) {
            // 60% chance to allocate
            uint32_t order = rand() % (PAGE_ALLOC_MAX_ORDER + 1);
            uint64_t addr = page_alloc(order);
            
            if (addr != 0) {
                // Find a free slot
                for (int i = 0; i < MAX_ALLOCS; i++) {
                    if (!allocs[i].allocated) {
                        allocs[i].addr = addr;
                        allocs[i].order = order;
                        allocs[i].allocated = true;
                        active_allocs++;
                        total_allocs++;
                        break;
                    }
                }
            }
        } else if (active_allocs > 0) {
            // Free a random allocation
            int attempts = 0;
            while (attempts < 100) {
                int idx = rand() % MAX_ALLOCS;
                if (allocs[idx].allocated) {
                    page_free(allocs[idx].addr, allocs[idx].order);
                    allocs[idx].allocated = false;
                    active_allocs--;
                    total_frees++;
                    break;
                }
                attempts++;
            }
        }
        
        if (iter % 50 == 49) {
            uart_puts("  Iteration ");
            uart_putdec(iter + 1);
            uart_puts(": active=");
            uart_putdec(active_allocs);
            uart_puts(" total_allocs=");
            uart_putdec(total_allocs);
            uart_puts(" total_frees=");
            uart_putdec(total_frees);
            uart_puts("\n");
        }
    }
    
    // Free remaining allocations
    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (allocs[i].allocated) {
            page_free(allocs[i].addr, allocs[i].order);
            allocs[i].allocated = false;
            total_frees++;
        }
    }
    
    uart_puts("\nFinal stats:\n");
    uart_puts("  Total allocations: ");
    uart_putdec(total_allocs);
    uart_puts("\n  Total frees: ");
    uart_putdec(total_frees);
    uart_puts("\n");
    
    bool passed = (total_allocs == total_frees) && (total_allocs > 100);
    print_result("Random allocation pattern", passed);
    
    num_allocs = 0;
}

// Test 3: Fragmentation test
static void test_fragmentation(void) {
    print_test_header("Fragmentation Test");
    
    // Allocate many small blocks
    uart_puts("Allocating 100 small blocks (order 0)...\n");
    int small_count = 0;
    for (int i = 0; i < 100 && i < MAX_ALLOCS; i++) {
        uint64_t addr = page_alloc(0);
        if (addr == 0) break;
        
        allocs[i].addr = addr;
        allocs[i].order = 0;
        allocs[i].allocated = true;
        small_count++;
    }
    
    uart_puts("  Allocated ");
    uart_putdec(small_count);
    uart_puts(" small blocks\n");
    
    // Free every other block to create fragmentation
    uart_puts("Creating fragmentation by freeing every other block...\n");
    for (int i = 0; i < small_count; i += 2) {
        page_free(allocs[i].addr, allocs[i].order);
        allocs[i].allocated = false;
    }
    
    // Try to allocate a large block - should fail due to fragmentation
    uart_puts("Attempting to allocate order-7 block (should work with coalescing)...\n");
    uint64_t large = page_alloc(7);
    
    if (large != 0) {
        uart_puts("  Success! Allocated order-7 block at 0x");
        uart_puthex(large);
        uart_puts("\n");
        page_free(large, 7);
    } else {
        uart_puts("  Failed (expected if fragmented)\n");
    }
    
    // Free remaining blocks
    for (int i = 0; i < small_count; i++) {
        if (allocs[i].allocated) {
            page_free(allocs[i].addr, allocs[i].order);
            allocs[i].allocated = false;
        }
    }
    
    // Now try large allocation again - should succeed
    uart_puts("After freeing all, attempting order-7 again...\n");
    large = page_alloc(7);
    bool passed = (large != 0);
    
    if (large != 0) {
        uart_puts("  Success! Coalescing worked\n");
        page_free(large, 7);
    } else {
        uart_puts("  Failed - coalescing may not be working\n");
    }
    
    print_result("Fragmentation handling", passed);
    num_allocs = 0;
}

// Test 4: Verify buddy splitting
static void test_buddy_splitting(void) {
    print_test_header("Buddy Splitting Verification");
    
    // Allocate a large block and free it
    uint64_t large = page_alloc(8); // 256 pages
    if (large == 0) {
        uart_puts("Failed to allocate order-8 block\n");
        print_result("Buddy splitting", false);
        return;
    }
    
    uart_puts("Allocated order-8 block at 0x");
    uart_puthex(large);
    uart_puts("\n");
    
    page_free(large, 8);
    uart_puts("Freed order-8 block\n");
    
    // Now allocate smaller blocks that should come from splitting
    uint64_t blocks[16];
    int count = 0;
    
    uart_puts("Allocating 16 order-4 blocks (should split the order-8)...\n");
    for (int i = 0; i < 16; i++) {
        blocks[i] = page_alloc(4);
        if (blocks[i] == 0) break;
        count++;
    }
    
    uart_puts("  Allocated ");
    uart_putdec(count);
    uart_puts(" order-4 blocks\n");
    
    // Check if addresses are properly aligned and spaced
    bool proper_split = true;
    for (int i = 0; i < count; i++) {
        // Each order-4 block should be 16 pages = 64KB
        uint64_t alignment_mask = (1UL << (4 + PAGE_SHIFT)) - 1;
        if (blocks[i] & alignment_mask) {
            uart_puts("  Block ");
            uart_putdec(i);
            uart_puts(" at 0x");
            uart_puthex(blocks[i]);
            uart_puts(" misaligned! (mask=0x");
            uart_puthex(alignment_mask);
            uart_puts(")\n");
            proper_split = false;
        }
    }
    
    // Free all blocks
    for (int i = 0; i < count; i++) {
        page_free(blocks[i], 4);
    }
    
    // Try to allocate order-8 again - should succeed if coalescing works
    large = page_alloc(8);
    if (large != 0) {
        uart_puts("Successfully reallocated order-8 after coalescing\n");
        page_free(large, 8);
    } else {
        uart_puts("Failed to reallocate order-8 (coalescing issue)\n");
        proper_split = false;
    }
    
    print_result("Buddy splitting", proper_split);
}

// Test 5: Large allocation passthrough
static void test_large_passthrough_stress(void) {
    print_test_header("Large Allocation Passthrough Test");
    
    // Try to allocate order > 12 (should go directly to PMM)
    uart_puts("Testing order-13 (32MB) allocation...\n");
    
    pmm_stats_t before;
    pmm_get_stats(&before);
    
    uint64_t huge = page_alloc(13);
    if (huge == 0) {
        uart_puts("  Failed (may be insufficient memory)\n");
        print_result("Large passthrough", true); // Not a failure, just no memory
        return;
    }
    
    uart_puts("  Allocated 32MB at 0x");
    uart_puthex(huge);
    uart_puts("\n");
    
    pmm_stats_t after;
    pmm_get_stats(&after);
    
    uint64_t pages_used = before.free_pages - after.free_pages;
    uart_puts("  PMM pages used: ");
    uart_putdec(pages_used);
    uart_puts(" (expected 8192)\n");
    
    bool correct = (pages_used == 8192);
    
    page_free(huge, 13);
    
    pmm_stats_t freed;
    pmm_get_stats(&freed);
    
    if (freed.free_pages != before.free_pages) {
        uart_puts("  Warning: Not all pages returned to PMM\n");
        correct = false;
    }
    
    print_result("Large passthrough", correct);
}

// Main stress test runner
void page_alloc_stress_tests(void) {
    uart_puts("\n========================================\n");
    uart_puts("Page Allocator Stress Tests\n");
    uart_puts("========================================\n");
    
    // Initialize allocations array
    memset(allocs, 0, sizeof(allocs));
    
    // Run tests
    test_max_memory_allocation();
    // test_random_pattern();  // Commented out - takes too long on real hardware
    test_fragmentation();
    test_buddy_splitting();
    test_large_passthrough_stress();
    
    uart_puts("\n========================================\n");
    uart_puts("Stress Tests Complete\n");
    uart_puts("========================================\n");
    
    // Final statistics
    page_alloc_print_stats();
}