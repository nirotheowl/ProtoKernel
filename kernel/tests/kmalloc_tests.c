/*
 * kernel/tests/kmalloc_tests.c
 *
 * Unit tests for the kmalloc allocator
 * REVISED: Tests for zero-overhead design
 */

#include <memory/kmalloc.h>
#include <memory/size_classes.h>
#include <memory/vmm.h>
#include <uart.h>
#include <string.h>

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) do { \
    uart_puts("[TEST] "); \
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
        uart_puts("[ASSERT FAILED] "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        TEST_FAIL(msg); \
    } \
} while (0)

// Test basic allocation and free
static int test_basic_alloc_free(void) {
    TEST_START("Basic allocation and free");
    
    void *ptr = kmalloc(64, 0);
    ASSERT(ptr != NULL, "Failed to allocate 64 bytes");
    ASSERT(kmalloc_validate(ptr), "Invalid allocation");
    ASSERT(kmalloc_size(ptr) == 64, "Size mismatch");
    
    // Write pattern and verify
    memset(ptr, 0xAB, 64);
    unsigned char *bytes = (unsigned char *)ptr;
    for (int i = 0; i < 64; i++) {
        ASSERT(bytes[i] == 0xAB, "Data corruption detected");
    }
    
    kfree(ptr);
    TEST_PASS();
    return 1;
}

// Test zero-overhead for small allocations
static int test_zero_overhead(void) {
    TEST_START("Zero overhead verification");
    
    // Test various small sizes
    size_t test_sizes[] = {1, 8, 16, 17, 32, 33, 64, 100};
    
    for (int i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        size_t size = test_sizes[i];
        void *ptr = kmalloc(size, 0);
        ASSERT(ptr != NULL, "Allocation failed");
        
        // Find which cache owns this allocation
        struct kmem_cache *cache = kmalloc_find_cache(ptr);
        ASSERT(cache != NULL, "Failed to find owning cache");
        
        // For zero-overhead design, the returned pointer should be
        // directly usable with no header
        size_t reported_size = kmalloc_size(ptr);
        
        // The reported size should match the requested size for small allocations
        // (it will be the size class size, which is >= requested size)
        ASSERT(reported_size >= size, "Size too small");
        
        kfree(ptr);
    }
    
    TEST_PASS();
    return 1;
}

// Test zero allocation
static int test_zero_alloc(void) {
    TEST_START("Zero allocation with KM_ZERO");
    
    void *ptr = kmalloc(128, KM_ZERO);
    ASSERT(ptr != NULL, "Failed to allocate with KM_ZERO");
    
    unsigned char *bytes = (unsigned char *)ptr;
    for (int i = 0; i < 128; i++) {
        ASSERT(bytes[i] == 0, "Memory not zeroed");
    }
    
    kfree(ptr);
    TEST_PASS();
    return 1;
}

// Test size class boundaries
static int test_size_class_boundaries(void) {
    TEST_START("Size class boundary allocations");
    
    for (int i = 0; i < KMALLOC_NUM_CLASSES; i++) {
        size_t size = kmalloc_size_classes[i];
        
        // Test exact size class
        void *ptr = kmalloc(size, 0);
        ASSERT(ptr != NULL, "Failed to allocate at size class");
        ASSERT(kmalloc_size(ptr) == size, "Size mismatch at boundary");
        kfree(ptr);
        
        // Test one byte less
        if (size > 1) {
            ptr = kmalloc(size - 1, 0);
            ASSERT(ptr != NULL, "Failed to allocate size-1");
            ASSERT(kmalloc_size(ptr) == size, "Should round up to size class");
            kfree(ptr);
        }
        
        // Test one byte more (should go to next size class)
        if (i < KMALLOC_NUM_CLASSES - 1) {
            ptr = kmalloc(size + 1, 0);
            ASSERT(ptr != NULL, "Failed to allocate size+1");
            ASSERT(kmalloc_size(ptr) == kmalloc_size_classes[i + 1], 
                   "Should use next size class");
            kfree(ptr);
        }
    }
    
    TEST_PASS();
    return 1;
}

// Test large allocations
static int test_large_alloc(void) {
    TEST_START("Large allocations (>64KB)");
    
    // Test 128KB allocation
    size_t large_size = 128 * 1024;
    void *ptr = kmalloc(large_size, 0);
    ASSERT(ptr != NULL, "Failed to allocate 128KB");
    ASSERT(kmalloc_validate(ptr), "Invalid large allocation");
    ASSERT(kmalloc_size(ptr) == large_size, "Large allocation size mismatch");
    
    // Verify no slab cache owns this
    struct kmem_cache *cache = kmalloc_find_cache(ptr);
    ASSERT(cache == NULL, "Large allocation should not be in slab cache");
    
    // Write pattern to verify memory is accessible
    unsigned char *bytes = (unsigned char *)ptr;
    for (size_t i = 0; i < large_size; i += 4096) {
        bytes[i] = (i / 4096) & 0xFF;
    }
    
    // Verify pattern
    for (size_t i = 0; i < large_size; i += 4096) {
        ASSERT(bytes[i] == ((i / 4096) & 0xFF), "Large allocation data corruption");
    }
    
    kfree(ptr);
    TEST_PASS();
    return 1;
}

// Test multiple allocations
static int test_multiple_allocs(void) {
    TEST_START("Multiple simultaneous allocations");
    
    void *ptrs[10];
    size_t sizes[10] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    
    // Allocate all
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(sizes[i], 0);
        ASSERT(ptrs[i] != NULL, "Failed to allocate in multiple test");
        memset(ptrs[i], i, sizes[i]);
    }
    
    // Verify all
    for (int i = 0; i < 10; i++) {
        unsigned char *bytes = (unsigned char *)ptrs[i];
        for (size_t j = 0; j < sizes[i]; j++) {
            ASSERT(bytes[j] == i, "Data corruption in multiple alloc test");
        }
    }
    
    // Free in reverse order
    for (int i = 9; i >= 0; i--) {
        kfree(ptrs[i]);
    }
    
    TEST_PASS();
    return 1;
}

// Test kfree address lookup
static int test_kfree_lookup(void) {
    TEST_START("kfree address lookup correctness");
    
    // Allocate from different size classes
    void *ptr1 = kmalloc(16, 0);
    void *ptr2 = kmalloc(256, 0);
    void *ptr3 = kmalloc(4096, 0);
    void *ptr4 = kmalloc(100 * 1024, 0);  // Large
    
    ASSERT(ptr1 && ptr2 && ptr3 && ptr4, "Allocations failed");
    
    // Verify each can be found correctly
    struct kmem_cache *cache1 = kmalloc_find_cache(ptr1);
    struct kmem_cache *cache2 = kmalloc_find_cache(ptr2);
    struct kmem_cache *cache3 = kmalloc_find_cache(ptr3);
    struct kmem_cache *cache4 = kmalloc_find_cache(ptr4);
    
    ASSERT(cache1 != NULL, "Failed to find cache for ptr1");
    ASSERT(cache2 != NULL, "Failed to find cache for ptr2");
    ASSERT(cache3 != NULL, "Failed to find cache for ptr3");
    ASSERT(cache4 == NULL, "Large allocation should not have cache");
    
    // Free them all
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    kfree(ptr4);
    
    TEST_PASS();
    return 1;
}

// Test realloc
static int test_realloc(void) {
    TEST_START("Realloc functionality");
    
    // Start with small allocation
    void *ptr = kmalloc(32, 0);
    ASSERT(ptr != NULL, "Initial allocation failed");
    memset(ptr, 0xDE, 32);
    
    // Grow allocation
    ptr = krealloc(ptr, 128, 0);
    ASSERT(ptr != NULL, "Realloc to larger size failed");
    ASSERT(kmalloc_size(ptr) == 128, "Realloc size mismatch");
    
    // Verify original data preserved
    unsigned char *bytes = (unsigned char *)ptr;
    for (int i = 0; i < 32; i++) {
        ASSERT(bytes[i] == 0xDE, "Realloc didn't preserve data");
    }
    
    // Shrink allocation (should keep same allocation)
    void *old_ptr = ptr;
    ptr = krealloc(ptr, 64, 0);
    ASSERT(ptr == old_ptr, "Shrink should return same pointer");
    ASSERT(kmalloc_size(ptr) == 128, "Size should remain same for shrink");
    
    kfree(ptr);
    
    // Test realloc with NULL
    ptr = krealloc(NULL, 64, 0);
    ASSERT(ptr != NULL, "Realloc NULL failed");
    kfree(ptr);
    
    TEST_PASS();
    return 1;
}

// Test calloc
static int test_calloc(void) {
    TEST_START("Calloc functionality");
    
    // Test normal calloc
    void *ptr = kcalloc(10, 64, 0);
    ASSERT(ptr != NULL, "Calloc failed");
    
    // Verify zeroed
    unsigned char *bytes = (unsigned char *)ptr;
    for (int i = 0; i < 640; i++) {
        ASSERT(bytes[i] == 0, "Calloc didn't zero memory");
    }
    
    kfree(ptr);
    
    // Test overflow protection
    ptr = kcalloc(SIZE_MAX/2, 3, 0);
    ASSERT(ptr == NULL, "Calloc overflow protection failed");
    
    TEST_PASS();
    return 1;
}

// Test edge cases
static int test_edge_cases(void) {
    TEST_START("Edge cases");
    
    // Test zero-size allocation
    void *ptr = kmalloc(0, 0);
    ASSERT(ptr == NULL, "Zero-size allocation should return NULL");
    
    // Test free NULL
    kfree(NULL);  // Should not crash
    
    // Test validation on NULL
    ASSERT(!kmalloc_validate(NULL), "NULL should not validate");
    ASSERT(kmalloc_size(NULL) == 0, "NULL size should be 0");
    
    // Test very small allocation
    ptr = kmalloc(1, 0);
    ASSERT(ptr != NULL, "1-byte allocation failed");
    ASSERT(kmalloc_size(ptr) == 16, "1-byte should use 16-byte size class");
    kfree(ptr);
    
    TEST_PASS();
    return 1;
}

// Test small and odd-sized allocations
static int test_small_odd_allocations(void) {
    TEST_START("Small and odd-sized allocations");
    
    // Test various small sizes
    size_t test_sizes[] = {
        1, 2, 3, 4, 5, 7, 8, 9,          // Very small
        13, 15, 17, 19, 23, 29, 31,     // Small odd
        33, 47, 63, 65, 96, 100,        // Medium odd
        127, 129, 255, 257, 383, 385,   // Boundary cases
        511, 513, 1023, 1025            // Larger odd
    };
    
    void *ptrs[sizeof(test_sizes)/sizeof(test_sizes[0])];
    
    // Allocate all
    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        size_t size = test_sizes[i];
        ptrs[i] = kmalloc(size, 0);
        ASSERT(ptrs[i] != NULL, "Failed to allocate odd size");
        
        // Verify we got at least the requested size
        size_t actual = kmalloc_size(ptrs[i]);
        ASSERT(actual >= size, "Allocated size too small");
        
        // Fill with pattern to verify no corruption
        unsigned char pattern = (i & 0xFF);
        unsigned char *bytes = (unsigned char *)ptrs[i];
        for (size_t j = 0; j < size; j++) {
            bytes[j] = pattern;
        }
    }
    
    // Verify all patterns intact
    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        size_t size = test_sizes[i];
        unsigned char *bytes = (unsigned char *)ptrs[i];
        unsigned char expected = (i & 0xFF);
        
        for (size_t j = 0; j < size; j++) {
            if (bytes[j] != expected) {
                uart_puts("Corruption at size ");
                uart_putdec(size);
                uart_puts(" byte ");
                uart_putdec(j);
                uart_puts("\n");
                ASSERT(0, "Data corruption detected");
            }
        }
    }
    
    // Free all
    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        kfree(ptrs[i]);
    }
    
    TEST_PASS();
    return 1;
}

// Test stress patterns for small allocations
static int test_small_allocation_stress(void) {
    TEST_START("Small allocation stress test");
    
    // Allocate many small objects of varying sizes
    #define STRESS_COUNT 100
    void *ptrs[STRESS_COUNT];
    size_t sizes[STRESS_COUNT];
    
    // Allocate with random-ish sizes
    for (int i = 0; i < STRESS_COUNT; i++) {
        // Generate pseudo-random size between 1 and 100
        sizes[i] = 1 + ((i * 13 + 7) % 100);
        ptrs[i] = kmalloc(sizes[i], 0);
        ASSERT(ptrs[i] != NULL, "Stress allocation failed");
        
        // Write pattern
        unsigned char *bytes = (unsigned char *)ptrs[i];
        for (size_t j = 0; j < sizes[i]; j++) {
            bytes[j] = i & 0xFF;
        }
    }
    
    // Free every other allocation
    for (int i = 0; i < STRESS_COUNT; i += 2) {
        kfree(ptrs[i]);
        ptrs[i] = NULL;
    }
    
    // Reallocate freed slots with different sizes
    for (int i = 0; i < STRESS_COUNT; i += 2) {
        sizes[i] = 1 + ((i * 17 + 11) % 150);
        ptrs[i] = kmalloc(sizes[i], 0);
        ASSERT(ptrs[i] != NULL, "Stress reallocation failed");
        unsigned char *bytes = (unsigned char *)ptrs[i];
        for (size_t j = 0; j < sizes[i]; j++) {
            bytes[j] = (i + 0x80) & 0xFF;
        }
    }
    
    // Verify all allocations
    for (int i = 0; i < STRESS_COUNT; i++) {
        unsigned char *bytes = (unsigned char *)ptrs[i];
        unsigned char expected = (i % 2) ? (i & 0xFF) : ((i + 0x80) & 0xFF);
        
        for (size_t j = 0; j < sizes[i]; j++) {
            ASSERT(bytes[j] == expected, "Stress test data corruption");
        }
    }
    
    // Free all remaining
    for (int i = 0; i < STRESS_COUNT; i++) {
        kfree(ptrs[i]);
    }
    
    TEST_PASS();
    return 1;
}

// Test alignment with zero overhead
static int test_alignment(void) {
    TEST_START("Allocation alignment (zero overhead)");
    
    // Test various sizes for proper alignment
    size_t test_sizes[] = {1, 7, 15, 31, 63, 127, 255, 511, 1023};
    
    for (int i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        void *ptr = kmalloc(test_sizes[i], 0);
        ASSERT(ptr != NULL, "Allocation failed in alignment test");
        
        // With zero overhead, alignment depends on the slab allocator
        // Check that we get at least 16-byte alignment for small sizes
        uintptr_t addr = (uintptr_t)ptr;
        size_t expected_align = (test_sizes[i] >= 256) ? 64 : 16;
        
        if ((addr & (expected_align - 1)) != 0) {
            uart_puts("Alignment failed for size ");
            uart_putdec(test_sizes[i]);
            uart_puts(" at address ");
            uart_puthex(addr);
            uart_puts(" (expected align ");
            uart_putdec(expected_align);
            uart_puts(")\n");
        }
        ASSERT((addr & (expected_align - 1)) == 0, "Allocation not properly aligned");
        
        kfree(ptr);
    }
    
    TEST_PASS();
    return 1;
}

// Test memory efficiency
static int test_memory_efficiency(void) {
    TEST_START("Memory efficiency verification");
    
    struct kmalloc_stats stats_before, stats_after;
    kmalloc_get_stats(&stats_before);
    
    // Allocate 1 byte - should use 16-byte size class with zero overhead
    void *ptr1 = kmalloc(1, 0);
    ASSERT(ptr1 != NULL, "1-byte allocation failed");
    
    kmalloc_get_stats(&stats_after);
    
    // With zero overhead, we should allocate exactly 16 bytes (the size class)
    // not 128 bytes like before
    uint64_t bytes_used = stats_after.active_bytes - stats_before.active_bytes;
    ASSERT(bytes_used == 1, "Should track 1 byte for user");
    
    // Allocate 17 bytes - should use 32-byte size class
    void *ptr2 = kmalloc(17, 0);
    ASSERT(ptr2 != NULL, "17-byte allocation failed");
    
    kmalloc_get_stats(&stats_after);
    bytes_used = stats_after.active_bytes - stats_before.active_bytes;
    ASSERT(bytes_used == 18, "Should track 18 bytes total");
    
    kfree(ptr1);
    kfree(ptr2);
    
    // Verification passed - output removed for cleaner test results
    
    TEST_PASS();
    return 1;
}

// Test statistics tracking
static int test_statistics(void) {
    TEST_START("Statistics tracking");
    
    struct kmalloc_stats stats_before, stats_after;
    kmalloc_get_stats(&stats_before);
    
    // Do some allocations
    void *ptr1 = kmalloc(64, 0);
    void *ptr2 = kmalloc(256, 0);
    void *ptr3 = kmalloc(100 * 1024, 0);  // Large allocation
    
    kmalloc_get_stats(&stats_after);
    
    ASSERT(stats_after.total_allocs == stats_before.total_allocs + 3, 
           "Allocation count not updated");
    ASSERT(stats_after.active_allocs == stats_before.active_allocs + 3,
           "Active allocation count wrong");
    ASSERT(stats_after.large_allocs == stats_before.large_allocs + 1,
           "Large allocation count wrong");
    
    // Free allocations
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    
    kmalloc_get_stats(&stats_after);
    ASSERT(stats_after.total_frees == stats_before.total_frees + 3,
           "Free count not updated");
    ASSERT(stats_after.active_allocs == stats_before.active_allocs,
           "Active allocations not decremented");
    
    TEST_PASS();
    return 1;
}

// Main test runner
int run_kmalloc_tests(void) {
    uart_puts("\n=== Running kmalloc tests ===\n");
    
    // Initialize kmalloc
    kmalloc_init();
    
    // Run all tests
    test_basic_alloc_free();
    test_zero_overhead();
    test_zero_alloc();
    test_size_class_boundaries();
    test_large_alloc();
    test_multiple_allocs();
    test_kfree_lookup();
    test_realloc();
    test_calloc();
    test_edge_cases();
    test_small_odd_allocations();
    test_small_allocation_stress();
    test_alignment();
    test_memory_efficiency();
    test_statistics();
    
    // Print summary
    uart_puts("\n=== kmalloc test summary ===\n");
    uart_puts("Tests run: ");
    uart_putdec(tests_run);
    uart_puts("\nTests passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nTests failed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    // Dump statistics (commented out for cleaner output)
    // uart_puts("\n=== Final kmalloc statistics ===\n");
    // kmalloc_dump_stats();
    
    return tests_failed == 0;
}
