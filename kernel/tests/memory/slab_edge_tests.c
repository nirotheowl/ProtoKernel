/*
 * kernel/tests/slab_edge_tests.c
 *
 * Comprehensive edge case tests for the slab allocator
 */

#include <memory/slab.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <uart.h>
#include <string.h>

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) do { \
    uart_puts("[EDGE TEST] "); \
    uart_puts(name); \
    uart_puts(" ... "); \
    tests_run++; \
} while (0)

#define TEST_PASS() do { \
    uart_puts("PASS\n"); \
    tests_passed++; \
} while (0)

#define TEST_FAIL(msg) do { \
    uart_puts("FAIL: "); \
    uart_puts(msg); \
    uart_puts("\n"); \
    tests_failed++; \
    return 0; \
} while (0)

#define ASSERT(condition, msg) do { \
    if (!(condition)) { \
        TEST_FAIL(msg); \
    } \
} while (0)

// Test 1: Maximum allocation count - fill entire slabs
static int test_max_allocations(void) {
    TEST_START("test_max_allocations");
    
    struct kmem_cache *cache = kmem_cache_create("max_alloc", 64, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Calculate expected objects per slab
    // With 64-byte objects in 4KB pages, accounting for slab header
    // This should match what the allocator calculates
    void *ptrs[1024]; // Should be more than enough
    int count = 0;
    
    // Allocate until failure
    while (count < 1024) {
        void *ptr = kmem_cache_alloc(cache, KM_NOSLEEP);
        if (!ptr) break;
        ptrs[count++] = ptr;
        
        // Write pattern to verify memory
        memset(ptr, count & 0xFF, 64);
    }
    
    uart_puts("\n  Allocated ");
    uart_putdec(count);
    uart_puts(" objects\n");
    
    // Verify all allocations are valid and distinct
    for (int i = 0; i < count; i++) {
        unsigned char *p = (unsigned char *)ptrs[i];
        for (int j = 0; j < 64; j++) {
            ASSERT(p[j] == ((i + 1) & 0xFF), "Memory corruption detected");
        }
        
        // Check no duplicates
        for (int j = i + 1; j < count; j++) {
            ASSERT(ptrs[i] != ptrs[j], "Duplicate allocation detected");
        }
    }
    
    // Free all
    for (int i = 0; i < count; i++) {
        kmem_cache_free(cache, ptrs[i]);
    }
    
    // Should be able to allocate again
    void *ptr = kmem_cache_alloc(cache, KM_NOSLEEP);
    ASSERT(ptr != NULL, "Cannot allocate after freeing all");
    kmem_cache_free(cache, ptr);
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Test 2: Alignment requirements
static int test_alignment_edge_cases(void) {
    TEST_START("test_alignment_edge_cases");
    
    // Test various alignments
    size_t alignments[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};
    
    for (int i = 0; i < sizeof(alignments)/sizeof(alignments[0]); i++) {
        char name[32];
        strcpy(name, "align_test_");
        int pos = strlen(name);
        int val = alignments[i];
        if (val >= 100) name[pos++] = '0' + (val / 100);
        if (val >= 10) name[pos++] = '0' + ((val / 10) % 10);
        name[pos++] = '0' + (val % 10);
        name[pos] = '\0';
        
        struct kmem_cache *cache = kmem_cache_create(name, 37, alignments[i], 0);
        ASSERT(cache != NULL, "Failed to create cache with alignment");
        
        // Allocate several objects
        void *ptrs[10];
        for (int j = 0; j < 10; j++) {
            ptrs[j] = kmem_cache_alloc(cache, KM_NOSLEEP);
            ASSERT(ptrs[j] != NULL, "Allocation failed");
            
            // Check alignment
            uintptr_t addr = (uintptr_t)ptrs[j];
            ASSERT((addr & (alignments[i] - 1)) == 0, "Incorrect alignment");
        }
        
        // Free all
        for (int j = 0; j < 10; j++) {
            kmem_cache_free(cache, ptrs[j]);
        }
        
        kmem_cache_destroy(cache);
    }
    
    TEST_PASS();
    return 1;
}

// Test 3: Zero-size and huge-size allocations
static int test_size_boundaries(void) {
    TEST_START("test_size_boundaries");
    
    // Test zero size (should fail)
    struct kmem_cache *cache = kmem_cache_create("zero_size", 0, 8, 0);
    ASSERT(cache == NULL, "Zero-size cache should fail");
    
    // Test maximum size (128KB is our current max)
    cache = kmem_cache_create("max_size", 131072, 8, 0);
    if (cache != NULL) {
        void *ptr = kmem_cache_alloc(cache, KM_NOSLEEP);
        if (ptr != NULL) {
            // Write to entire allocation
            memset(ptr, 0xAA, 131072);
            kmem_cache_free(cache, ptr);
        }
        kmem_cache_destroy(cache);
    }
    
    // Test over maximum size (should fail)
    cache = kmem_cache_create("over_max", 131073, 8, 0);
    ASSERT(cache == NULL, "Over-max size cache should fail");
    
    // Test odd sizes
    size_t odd_sizes[] = {1, 3, 7, 13, 17, 31, 63, 127, 255, 511, 1023};
    for (int i = 0; i < sizeof(odd_sizes)/sizeof(odd_sizes[0]); i++) {
        char name[32] = "odd_size";
        cache = kmem_cache_create(name, odd_sizes[i], 1, 0);
        ASSERT(cache != NULL, "Failed to create odd-size cache");
        
        void *ptr = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(ptr != NULL, "Failed to allocate odd size");
        
        // Write full size
        memset(ptr, 0x55, odd_sizes[i]);
        kmem_cache_free(cache, ptr);
        kmem_cache_destroy(cache);
    }
    
    TEST_PASS();
    return 1;
}

// Test 4: Double-free detection
static int test_double_free(void) {
    TEST_START("test_double_free");
    
    struct kmem_cache *cache = kmem_cache_create("double_free", 128, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    void *ptr1 = kmem_cache_alloc(cache, KM_NOSLEEP);
    void *ptr2 = kmem_cache_alloc(cache, KM_NOSLEEP);
    ASSERT(ptr1 != NULL && ptr2 != NULL, "Allocation failed");
    
    // Free once
    kmem_cache_free(cache, ptr1);
    
    // Double free - our current implementation might not detect this
    // but we should at least not crash
    kmem_cache_free(cache, ptr1);
    
    // Try to allocate - should still work
    void *ptr3 = kmem_cache_alloc(cache, KM_NOSLEEP);
    ASSERT(ptr3 != NULL, "Allocation failed after double free");
    
    // Clean up
    kmem_cache_free(cache, ptr2);
    kmem_cache_free(cache, ptr3);
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test 5: Free NULL pointer
static int test_free_null(void) {
    TEST_START("test_free_null");
    
    struct kmem_cache *cache = kmem_cache_create("free_null", 64, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Free NULL - should not crash
    kmem_cache_free(cache, NULL);
    kmem_cache_free(NULL, NULL);
    
    // Cache should still work
    void *ptr = kmem_cache_alloc(cache, KM_NOSLEEP);
    ASSERT(ptr != NULL, "Allocation failed after NULL free");
    
    kmem_cache_free(cache, ptr);
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test 6: Allocation pattern stress test
static int test_allocation_patterns(void) {
    TEST_START("test_allocation_patterns");
    
    struct kmem_cache *cache = kmem_cache_create("pattern_test", 96, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    void *ptrs[256];
    memset(ptrs, 0, sizeof(ptrs));
    
    // Pattern 1: Allocate every other slot, then fill gaps
    for (int i = 0; i < 128; i += 2) {
        ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(ptrs[i] != NULL, "Allocation failed");
        memset(ptrs[i], i, 96);
    }
    
    for (int i = 1; i < 128; i += 2) {
        ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(ptrs[i] != NULL, "Allocation failed");
        memset(ptrs[i], i, 96);
    }
    
    // Verify data integrity
    for (int i = 0; i < 128; i++) {
        unsigned char *p = (unsigned char *)ptrs[i];
        for (int j = 0; j < 96; j++) {
            ASSERT(p[j] == (i & 0xFF), "Data corruption");
        }
    }
    
    // Pattern 2: Free from middle outward
    for (int i = 0; i < 64; i++) {
        kmem_cache_free(cache, ptrs[64 - i - 1]);
        kmem_cache_free(cache, ptrs[64 + i]);
        ptrs[64 - i - 1] = NULL;
        ptrs[64 + i] = NULL;
    }
    
    // Pattern 3: Reallocate with different pattern
    for (int i = 0; i < 128; i++) {
        if (ptrs[i] == NULL) {
            ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
            ASSERT(ptrs[i] != NULL, "Reallocation failed");
            memset(ptrs[i], 0xFF - i, 96);
        }
    }
    
    // Free all
    for (int i = 0; i < 128; i++) {
        if (ptrs[i]) kmem_cache_free(cache, ptrs[i]);
    }
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Test 7: Multiple slabs management
static int test_multiple_slabs(void) {
    TEST_START("test_multiple_slabs");
    
    // Use larger objects to get fewer per slab
    struct kmem_cache *cache = kmem_cache_create("multi_slab", 512, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Get initial stats
    struct kmem_stats stats_before;
    kmem_cache_stats(cache, &stats_before);
    
    // Allocate enough to require multiple slabs
    void *ptrs[128];
    int allocated = 0;
    uint64_t slabs_seen = 1;
    
    for (int i = 0; i < 128; i++) {
        ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        if (!ptrs[i]) break;
        allocated++;
        
        // Check if we got a new slab
        struct kmem_stats stats;
        kmem_cache_stats(cache, &stats);
        if (stats.total_slabs > slabs_seen) {
            uart_puts("\n  New slab created at allocation ");
            uart_putdec(i + 1);
            uart_puts("\n");
            slabs_seen = stats.total_slabs;
        }
    }
    
    uart_puts("  Allocated ");
    uart_putdec(allocated);
    uart_puts(" objects across ");
    uart_putdec(slabs_seen);
    uart_puts(" slabs\n");
    
    ASSERT(slabs_seen > 1, "Should have created multiple slabs");
    
    // Free every third object
    for (int i = 0; i < allocated; i += 3) {
        kmem_cache_free(cache, ptrs[i]);
        ptrs[i] = NULL;
    }
    
    // Reallocate - should reuse freed slots
    int reallocated = 0;
    for (int i = 0; i < allocated; i += 3) {
        ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        if (ptrs[i]) reallocated++;
    }
    
    ASSERT(reallocated > 0, "Should be able to reallocate freed objects");
    
    // Free all
    for (int i = 0; i < allocated; i++) {
        if (ptrs[i]) kmem_cache_free(cache, ptrs[i]);
    }
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Test 8: Cache name limits
static int test_cache_names(void) {
    TEST_START("test_cache_names");
    
    // Test empty name
    struct kmem_cache *cache = kmem_cache_create("", 64, 8, 0);
    ASSERT(cache != NULL, "Empty name should be allowed");
    kmem_cache_destroy(cache);
    
    // Test maximum length name (15 chars + null terminator for 16 byte field)
    char long_name[40];
    for (int i = 0; i < 15; i++) long_name[i] = 'A' + (i % 26);
    long_name[15] = '\0';
    
    cache = kmem_cache_create(long_name, 64, 8, 0);
    ASSERT(cache != NULL, "Max length name should work");
    
    // Verify name was stored correctly (should be truncated if needed)
    ASSERT(strlen(cache->warm.name) <= 15, "Name too long");
    
    kmem_cache_destroy(cache);
    
    // Test very long name (should be truncated)
    for (int i = 0; i < 39; i++) long_name[i] = 'B';
    long_name[39] = '\0';
    
    cache = kmem_cache_create(long_name, 64, 8, 0);
    ASSERT(cache != NULL, "Over-length name should work (truncated)");
    ASSERT(strlen(cache->warm.name) == 15, "Name should be truncated");
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Test 9: Memory corruption detection
static int test_corruption_detection(void) {
    TEST_START("test_corruption_detection");
    
    struct kmem_cache *cache = kmem_cache_create("corrupt_test", 64, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Allocate several objects
    void *ptrs[5];
    for (int i = 0; i < 5; i++) {
        ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(ptrs[i] != NULL, "Allocation failed");
        memset(ptrs[i], 0x5A, 64);
    }
    
    // Intentionally write past the end of an allocation
    // This is undefined behavior but we want to see if it affects neighbors
    unsigned char *bad_ptr = (unsigned char *)ptrs[2];
    unsigned char saved = bad_ptr[64]; // Save what's there
    bad_ptr[64] = 0xFF; // Write past end
    
    // Check if neighbors are affected
    unsigned char *before = (unsigned char *)ptrs[1];
    unsigned char *after = (unsigned char *)ptrs[3];
    int before_ok = 1, after_ok = 1;
    
    for (int i = 0; i < 64; i++) {
        if (before[i] != 0x5A) before_ok = 0;
        if (after[i] != 0x5A) after_ok = 0;
    }
    
    // Restore original value
    bad_ptr[64] = saved;
    
    // Note: We don't ASSERT here because behavior is undefined
    // We just want to observe what happens
    uart_puts("\n  Overflow detection: ");
    if (before_ok && after_ok) {
        uart_puts("Neighbors unaffected (good isolation)\n");
    } else {
        uart_puts("WARNING: Overflow affected neighbors\n");
    }
    
    // Free all
    for (int i = 0; i < 5; i++) {
        kmem_cache_free(cache, ptrs[i]);
    }
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Test 10: Slab cache reuse after destroy/recreate
static int test_cache_recreate(void) {
    TEST_START("test_cache_recreate");
    
    // Create and destroy cache multiple times
    for (int iter = 0; iter < 5; iter++) {
        struct kmem_cache *cache = kmem_cache_create("recreate_test", 128, 16, 0);
        ASSERT(cache != NULL, "Failed to create cache");
        
        // Allocate some objects
        void *ptrs[10];
        for (int i = 0; i < 10; i++) {
            ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
            ASSERT(ptrs[i] != NULL, "Allocation failed");
            
            // Write pattern
            memset(ptrs[i], iter * 10 + i, 128);
        }
        
        // Verify pattern
        for (int i = 0; i < 10; i++) {
            unsigned char *p = (unsigned char *)ptrs[i];
            for (int j = 0; j < 128; j++) {
                ASSERT(p[j] == ((iter * 10 + i) & 0xFF), "Data corruption");
            }
        }
        
        // Free half
        for (int i = 0; i < 10; i += 2) {
            kmem_cache_free(cache, ptrs[i]);
        }
        
        // Destroy cache (with some objects still allocated - this is a leak test)
        kmem_cache_destroy(cache);
    }
    
    TEST_PASS();
    return 1;
}

// Test 11: Concurrent-style access pattern (simulated)
static int test_concurrent_pattern(void) {
    TEST_START("test_concurrent_pattern");
    
    struct kmem_cache *cache = kmem_cache_create("concurrent", 48, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Simulate multiple "threads" with different allocation patterns
    void *thread1_ptrs[50];
    void *thread2_ptrs[50];
    void *thread3_ptrs[50];
    int t1_count = 0, t2_count = 0, t3_count = 0;
    
    // Interleaved allocation pattern
    for (int round = 0; round < 20; round++) {
        // Thread 1: allocate 2, free 1
        if (t1_count < 50) {
            thread1_ptrs[t1_count++] = kmem_cache_alloc(cache, KM_NOSLEEP);
            if (t1_count < 50)
                thread1_ptrs[t1_count++] = kmem_cache_alloc(cache, KM_NOSLEEP);
        }
        if (t1_count > 10 && (round % 3 == 0)) {
            kmem_cache_free(cache, thread1_ptrs[--t1_count]);
        }
        
        // Thread 2: allocate 1, free 2
        if (t2_count < 50) {
            thread2_ptrs[t2_count++] = kmem_cache_alloc(cache, KM_NOSLEEP);
        }
        if (t2_count > 5) {
            kmem_cache_free(cache, thread2_ptrs[--t2_count]);
            if (t2_count > 0)
                kmem_cache_free(cache, thread2_ptrs[--t2_count]);
        }
        
        // Thread 3: bulk allocate then bulk free
        if (round < 10 && t3_count < 50) {
            thread3_ptrs[t3_count++] = kmem_cache_alloc(cache, KM_NOSLEEP);
        } else if (round >= 10 && t3_count > 0) {
            kmem_cache_free(cache, thread3_ptrs[--t3_count]);
        }
    }
    
    // Clean up remaining allocations
    while (t1_count > 0) kmem_cache_free(cache, thread1_ptrs[--t1_count]);
    while (t2_count > 0) kmem_cache_free(cache, thread2_ptrs[--t2_count]);
    while (t3_count > 0) kmem_cache_free(cache, thread3_ptrs[--t3_count]);
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Test 12: Verify stats accuracy
static int test_stats_accuracy(void) {
    TEST_START("test_stats_accuracy");
    
    struct kmem_cache *cache = kmem_cache_create("stats_test", 100, 4, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    struct kmem_stats stats;
    kmem_cache_stats(cache, &stats);
    
    // Initial state
    ASSERT(stats.allocs == 0, "Initial allocs should be 0");
    ASSERT(stats.frees == 0, "Initial frees should be 0");
    ASSERT(stats.active_objs == 0, "Initial active should be 0");
    
    // Track operations
    void *ptrs[20];
    
    // Allocate 20
    for (int i = 0; i < 20; i++) {
        ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(ptrs[i] != NULL, "Allocation failed");
    }
    
    kmem_cache_stats(cache, &stats);
    ASSERT(stats.allocs == 20, "Allocs count wrong");
    ASSERT(stats.active_objs == 20, "Active count wrong");
    ASSERT(stats.frees == 0, "Frees should still be 0");
    
    // Free 5
    for (int i = 0; i < 5; i++) {
        kmem_cache_free(cache, ptrs[i]);
    }
    
    kmem_cache_stats(cache, &stats);
    ASSERT(stats.allocs == 20, "Allocs should not change");
    ASSERT(stats.frees == 5, "Frees count wrong");
    ASSERT(stats.active_objs == 15, "Active count wrong after free");
    
    // Allocate 5 more
    for (int i = 0; i < 5; i++) {
        ptrs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
    }
    
    kmem_cache_stats(cache, &stats);
    ASSERT(stats.allocs == 25, "Allocs count wrong after realloc");
    ASSERT(stats.frees == 5, "Frees should not change");
    ASSERT(stats.active_objs == 20, "Active count wrong after realloc");
    
    // Free all
    for (int i = 0; i < 20; i++) {
        kmem_cache_free(cache, ptrs[i]);
    }
    
    kmem_cache_stats(cache, &stats);
    ASSERT(stats.frees == 25, "Final frees count wrong");
    ASSERT(stats.active_objs == 0, "Should have no active objects");
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Main test runner for edge cases
void run_slab_edge_tests(void) {
    uart_puts("\n=== Starting Slab Allocator Edge Case Tests ===\n");
    
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    
    // Run all edge case tests
    test_max_allocations();
    test_alignment_edge_cases();
    test_size_boundaries();
    test_double_free();
    test_free_null();
    test_allocation_patterns();
    test_multiple_slabs();
    test_cache_names();
    test_corruption_detection();
    test_cache_recreate();
    test_concurrent_pattern();
    test_stats_accuracy();
    
    // Summary
    uart_puts("\n=== Edge Test Summary ===\n");
    uart_puts("Tests run: ");
    uart_putdec(tests_run);
    uart_puts("\nTests passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nTests failed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("\nAll edge tests PASSED!\n");
    } else {
        uart_puts("\nSome edge tests FAILED!\n");
    }
}
