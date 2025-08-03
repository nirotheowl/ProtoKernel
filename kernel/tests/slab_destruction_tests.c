/*
 * kernel/tests/slab_destruction_tests.c
 *
 * Tests for slab allocator empty slab destruction and list integrity
 */

#include <memory/slab.h>
#include <memory/pmm.h>
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
        TEST_FAIL(msg); \
    } \
} while (0)

// Helper to count slabs in a list
static int count_slabs_in_list(struct slab_list_node *head) {
    int count = 0;
    struct slab_list_node *node;
    
    for (node = head->next; node != head; node = node->next) {
        count++;
        if (count > 100) {
            uart_puts("[ERROR] List corruption detected - too many nodes\n");
            return -1;
        }
    }
    return count;
}

// Helper to verify all slabs in a list have valid cache pointers
static int verify_slab_cache_pointers(struct kmem_cache *cache) {
    struct slab_list_node *lists[] = {
        &cache->full_slabs,
        &cache->partial_slabs,
        &cache->empty_slabs
    };
    const char *list_names[] = {"full", "partial", "empty"};
    
    for (int i = 0; i < 3; i++) {
        struct slab_list_node *node;
        for (node = lists[i]->next; node != lists[i]; node = node->next) {
            struct kmem_slab *slab = (struct kmem_slab *)node;
            if (slab->cache != cache) {
                uart_puts("[ERROR] Slab in ");
                uart_puts(list_names[i]);
                uart_puts(" list has wrong cache pointer: expected ");
                uart_puthex((unsigned long)cache);
                uart_puts(", got ");
                uart_puthex((unsigned long)slab->cache);
                uart_puts("\n");
                return 0;
            }
        }
    }
    return 1;
}

// Test 1: Basic empty slab destruction
static int test_empty_slab_destruction(void) {
    TEST_START("Empty slab destruction");
    
    // Create a test cache
    struct kmem_cache *cache = kmem_cache_create("test-destroy", 64, 16, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Allocate objects to create multiple slabs
    void *objs[200];
    int num_objs = cache->objects_per_slab * 2 + 10; // Ensure we need at least 3 slabs
    
    uart_puts("  Allocating ");
    uart_putdec(num_objs);
    uart_puts(" objects (");
    uart_putdec(cache->objects_per_slab);
    uart_puts(" per slab)\n");
    
    for (int i = 0; i < num_objs; i++) {
        objs[i] = kmem_cache_alloc(cache, 0);
        ASSERT(objs[i] != NULL, "Allocation failed");
    }
    
    // Check slab counts
    uart_puts("  Initial state - total slabs: ");
    uart_putdec(cache->stats.total_slabs);
    uart_puts("\n");
    
    // Free all objects from one slab
    uart_puts("  Freeing first slab worth of objects\n");
    for (int i = 0; i < cache->objects_per_slab; i++) {
        kmem_cache_free(cache, objs[i]);
    }
    
    // Check that we have one empty slab
    int empty_count = count_slabs_in_list(&cache->empty_slabs);
    uart_puts("  Empty slabs after first batch free: ");
    uart_putdec(empty_count);
    uart_puts("\n");
    ASSERT(empty_count == 1, "Should have exactly one empty slab");
    
    // Free more objects to create second empty slab
    uart_puts("  Freeing second slab worth of objects\n");
    for (int i = cache->objects_per_slab; i < cache->objects_per_slab * 2; i++) {
        kmem_cache_free(cache, objs[i]);
    }
    
    // Should still have only one empty slab (second should be destroyed)
    empty_count = count_slabs_in_list(&cache->empty_slabs);
    uart_puts("  Empty slabs after second batch free: ");
    uart_putdec(empty_count);
    uart_puts("\n");
    ASSERT(empty_count == 1, "Should still have exactly one empty slab");
    
    // Verify cache pointers are still valid
    ASSERT(verify_slab_cache_pointers(cache), "Cache pointers corrupted");
    
    // Clean up
    for (int i = cache->objects_per_slab * 2; i < num_objs; i++) {
        kmem_cache_free(cache, objs[i]);
    }
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test 2: Stress test with rapid alloc/free cycles
static int test_rapid_alloc_free_cycles(void) {
    TEST_START("Rapid alloc/free cycles");
    
    struct kmem_cache *cache = kmem_cache_create("test-rapid", 128, 16, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    void *objs[50];
    
    // Do multiple cycles of allocation and freeing
    for (int cycle = 0; cycle < 5; cycle++) {
        uart_puts("  Cycle ");
        uart_putdec(cycle + 1);
        uart_puts("...\n");
        
        // Allocate
        for (int i = 0; i < 50; i++) {
            objs[i] = kmem_cache_alloc(cache, 0);
            ASSERT(objs[i] != NULL, "Allocation failed");
        }
        
        // Free in reverse order
        for (int i = 49; i >= 0; i--) {
            kmem_cache_free(cache, objs[i]);
        }
        
        // Verify list integrity
        ASSERT(verify_slab_cache_pointers(cache), "Cache pointers corrupted");
        
        // Check that we don't accumulate empty slabs
        int empty_count = count_slabs_in_list(&cache->empty_slabs);
        uart_puts("    Empty slabs: ");
        uart_putdec(empty_count);
        uart_puts("\n");
        ASSERT(empty_count <= 1, "Too many empty slabs");
    }
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Test 3: NOREAP flag prevents slab destruction
static int test_noreap_flag(void) {
    TEST_START("NOREAP flag behavior");
    
    struct kmem_cache *cache = kmem_cache_create("test-noreap", 64, 16, KMEM_CACHE_NOREAP);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Allocate objects to create multiple slabs
    void *objs[200];
    int num_objs = cache->objects_per_slab * 3;
    
    for (int i = 0; i < num_objs; i++) {
        objs[i] = kmem_cache_alloc(cache, 0);
        ASSERT(objs[i] != NULL, "Allocation failed");
    }
    
    uart_puts("  Created ");
    uart_putdec(cache->stats.total_slabs);
    uart_puts(" slabs\n");
    
    // Free all objects
    for (int i = 0; i < num_objs; i++) {
        kmem_cache_free(cache, objs[i]);
    }
    
    // With NOREAP, all slabs should remain as empty slabs
    int empty_count = count_slabs_in_list(&cache->empty_slabs);
    uart_puts("  Empty slabs with NOREAP: ");
    uart_putdec(empty_count);
    uart_puts("\n");
    ASSERT(empty_count == 3, "All slabs should remain with NOREAP");
    
    // Verify cache pointers
    ASSERT(verify_slab_cache_pointers(cache), "Cache pointers corrupted");
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Test 4: Mixed allocation patterns
static int test_mixed_patterns(void) {
    TEST_START("Mixed allocation patterns");
    
    struct kmem_cache *cache = kmem_cache_create("test-mixed", 256, 16, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    void *objs[100];
    
    // Pattern: allocate some, free some, allocate more
    uart_puts("  Phase 1: Initial allocations\n");
    for (int i = 0; i < 50; i++) {
        objs[i] = kmem_cache_alloc(cache, 0);
        ASSERT(objs[i] != NULL, "Allocation failed");
    }
    
    uart_puts("  Phase 2: Free every other object\n");
    for (int i = 0; i < 50; i += 2) {
        kmem_cache_free(cache, objs[i]);
        objs[i] = NULL;
    }
    
    uart_puts("  Phase 3: Allocate more\n");
    for (int i = 50; i < 100; i++) {
        objs[i] = kmem_cache_alloc(cache, 0);
        ASSERT(objs[i] != NULL, "Allocation failed");
    }
    
    uart_puts("  Phase 4: Free everything\n");
    for (int i = 0; i < 100; i++) {
        if (objs[i]) {
            kmem_cache_free(cache, objs[i]);
        }
    }
    
    // Verify final state
    ASSERT(verify_slab_cache_pointers(cache), "Cache pointers corrupted");
    ASSERT(cache->stats.active_objs == 0, "Objects leaked");
    
    int empty_count = count_slabs_in_list(&cache->empty_slabs);
    uart_puts("  Final empty slabs: ");
    uart_putdec(empty_count);
    uart_puts("\n");
    ASSERT(empty_count == 1, "Should have exactly one empty slab");
    
    kmem_cache_destroy(cache);
    TEST_PASS();
    return 1;
}

// Main test runner
int run_slab_destruction_tests(void) {
    uart_puts("\n=== Running slab destruction tests ===\n");
    
    // Initialize the slab allocator if not already done
    kmem_init();
    
    test_empty_slab_destruction();
    test_rapid_alloc_free_cycles();
    test_noreap_flag();
    test_mixed_patterns();
    
    // Print summary
    uart_puts("\n=== Slab destruction test summary ===\n");
    uart_puts("Tests run: ");
    uart_putdec(tests_run);
    uart_puts("\nTests passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nTests failed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    return tests_failed == 0;
}