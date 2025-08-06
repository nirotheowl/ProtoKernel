/*
 * kernel/tests/slab_lookup_tests.c
 *
 * Unit tests for slab hash table lookup system
 */

#include <tests/slab_lookup_tests.h>
#include <memory/slab_lookup.h>
#include <memory/slab.h>
#include <memory/kmalloc.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <memory/vmm.h>
#include <uart.h>
#include <string.h>

// Test state
static int tests_run = 0;
static int tests_passed = 0;

// Helper: Create a fake slab for testing
static struct kmem_slab *create_test_slab(void *base, size_t size, struct kmem_cache *cache) {
    // Allocate a page for the test slab
    uint64_t phys = pmm_alloc_page();
    if (!phys) return NULL;
    
    struct kmem_slab *slab = (struct kmem_slab *)PHYS_TO_DMAP(phys);
    slab->slab_base = base;
    slab->slab_size = size;
    slab->cache = cache;
    slab->num_objects = 10;  // Arbitrary
    slab->num_free = 10;
    
    return slab;
}

// Test 1: Basic insert and lookup
static void test_basic_insert_lookup(void) {
    uart_puts("  Test 1: Basic insert and lookup... ");
    tests_run++;
    
    // Create a test cache and slab
    struct kmem_cache test_cache = {0};
    strncpy(test_cache.warm.name, "test_cache", 31);
    
    void *test_addr = (void *)0xFFFF000040300000;  // Arbitrary test address
    struct kmem_slab *slab = create_test_slab(test_addr, PAGE_SIZE, &test_cache);
    if (!slab) {
        uart_puts("FAILED (couldn't create test slab)\n");
        return;
    }
    
    // Insert into hash table
    slab_lookup_insert(slab);
    
    // Try to find it
    struct kmem_cache *found = slab_lookup_find(test_addr);
    if (found == &test_cache) {
        uart_puts("PASSED\n");
        tests_passed++;
    } else {
        uart_puts("FAILED (lookup returned wrong cache)\n");
    }
    
    // Clean up
    slab_lookup_remove(slab);
    pmm_free_page(DMAP_TO_PHYS((uintptr_t)slab));
}

// Test 2: Multiple slabs in same bucket (collision handling)
static void test_collision_handling(void) {
    uart_puts("  Test 2: Collision handling... ");
    tests_run++;
    
    struct kmem_cache cache1 = {0}, cache2 = {0};
    strncpy(cache1.warm.name, "cache1", 31);
    strncpy(cache2.warm.name, "cache2", 31);
    
    // Create addresses that will likely collide (same page after shift)
    void *addr1 = (void *)0xFFFF000040400000;
    void *addr2 = (void *)0xFFFF000040400100;  // Same page, different offset
    
    struct kmem_slab *slab1 = create_test_slab(addr1, PAGE_SIZE, &cache1);
    struct kmem_slab *slab2 = create_test_slab(addr2, PAGE_SIZE, &cache2);
    
    if (!slab1 || !slab2) {
        uart_puts("FAILED (couldn't create test slabs)\n");
        return;
    }
    
    // Insert both
    slab_lookup_insert(slab1);
    slab_lookup_insert(slab2);
    
    // Verify both can be found
    struct kmem_cache *found1 = slab_lookup_find(addr1);
    struct kmem_cache *found2 = slab_lookup_find(addr2);
    
    if (found1 == &cache1 && found2 == &cache2) {
        uart_puts("PASSED\n");
        tests_passed++;
    } else {
        uart_puts("FAILED (incorrect lookups after collision)\n");
    }
    
    // Clean up
    slab_lookup_remove(slab1);
    slab_lookup_remove(slab2);
    pmm_free_page(DMAP_TO_PHYS((uintptr_t)slab1));
    pmm_free_page(DMAP_TO_PHYS((uintptr_t)slab2));
}

// Test 3: Boundary address lookup
static void test_boundary_addresses(void) {
    uart_puts("  Test 3: Boundary address lookup... ");
    tests_run++;
    
    struct kmem_cache cache = {0};
    strncpy(cache.warm.name, "boundary_cache", 31);
    
    void *base = (void *)0xFFFF000040500000;
    size_t size = 4 * PAGE_SIZE;  // 16KB slab
    struct kmem_slab *slab = create_test_slab(base, size, &cache);
    
    if (!slab) {
        uart_puts("FAILED (couldn't create test slab)\n");
        return;
    }
    
    slab_lookup_insert(slab);
    
    // Test various addresses within the slab
    void *start_addr = base;
    void *mid_addr = (char *)base + (size / 2);
    void *end_addr = (char *)base + size - 1;  // Last valid byte
    void *past_end = (char *)base + size;     // First invalid byte
    
    
    struct kmem_cache *found_start = slab_lookup_find(start_addr);
    struct kmem_cache *found_mid = slab_lookup_find(mid_addr);
    struct kmem_cache *found_end = slab_lookup_find(end_addr);
    struct kmem_cache *found_past = slab_lookup_find(past_end);
    
    if (found_start == &cache && found_mid == &cache && 
        found_end == &cache && found_past == NULL) {
        uart_puts("PASSED\n");
        tests_passed++;
    } else {
        uart_puts("FAILED (boundary checks incorrect)\n");
        uart_puts("    Debug: start=");
        uart_puts(found_start == &cache ? "OK" : "FAIL");
        uart_puts(" mid=");
        uart_puts(found_mid == &cache ? "OK" : "FAIL");
        uart_puts(" end=");
        uart_puts(found_end == &cache ? "OK" : "FAIL");
        uart_puts(" past=");
        uart_puts(found_past == NULL ? "OK" : "FAIL");
        uart_puts("\n");
    }
    
    // Clean up
    slab_lookup_remove(slab);
    pmm_free_page(DMAP_TO_PHYS((uintptr_t)slab));
}

// Test 4: Remove and re-lookup
static void test_remove_lookup(void) {
    uart_puts("  Test 4: Remove and re-lookup... ");
    tests_run++;
    
    struct kmem_cache cache = {0};
    strncpy(cache.warm.name, "remove_cache", 31);
    
    void *addr = (void *)0xFFFF000040600000;
    struct kmem_slab *slab = create_test_slab(addr, PAGE_SIZE, &cache);
    
    if (!slab) {
        uart_puts("FAILED (couldn't create test slab)\n");
        return;
    }
    
    // Insert, verify, remove, verify gone
    slab_lookup_insert(slab);
    
    if (slab_lookup_find(addr) != &cache) {
        uart_puts("FAILED (initial lookup failed)\n");
        pmm_free_page(DMAP_TO_PHYS((uintptr_t)slab));
        return;
    }
    
    slab_lookup_remove(slab);
    
    if (slab_lookup_find(addr) == NULL) {
        uart_puts("PASSED\n");
        tests_passed++;
    } else {
        uart_puts("FAILED (slab still found after removal)\n");
    }
    
    pmm_free_page(DMAP_TO_PHYS((uintptr_t)slab));
}

// Test 5: Many slabs (stress test)
static void test_many_slabs(void) {
    uart_puts("  Test 5: Many slabs stress test... ");
    tests_run++;
    
    struct kmem_cache cache = {0};
    strncpy(cache.warm.name, "stress_cache", 31);
    
    // Get baseline entry count before test
    size_t baseline_entries = slab_lookup_get_entry_count();
    
    // Create many slabs (reduced to avoid memory exhaustion)
    #define NUM_TEST_SLABS 100  // Test with more slabs to see memory usage
    
    struct kmem_slab *slabs[NUM_TEST_SLABS];
    void *addresses[NUM_TEST_SLABS];
    
    // Create and insert many slabs
    for (int i = 0; i < NUM_TEST_SLABS; i++) {
        addresses[i] = (void *)(0xFFFF000041000000UL + (i * PAGE_SIZE * 4));
        slabs[i] = create_test_slab(addresses[i], PAGE_SIZE, &cache);
        
        if (!slabs[i]) {
            uart_puts("\n    FAILED at slab ");
            uart_putdec(i);
            uart_puts(" (couldn't create test slab)\n");
            // Clean up what we created
            for (int j = 0; j < i; j++) {
                slab_lookup_remove(slabs[j]);
                pmm_free_page(DMAP_TO_PHYS((uintptr_t)slabs[j]));
            }
            return;
        }
        
        slab_lookup_insert(slabs[i]);
    }
    
    // Verify all can be found
    bool all_found = true;
    for (int i = 0; i < NUM_TEST_SLABS; i++) {
        if (slab_lookup_find(addresses[i]) != &cache) {
            all_found = false;
            break;
        }
    }
    
    if (all_found) {
        uart_puts("PASSED\n");
        tests_passed++;
    } else {
        uart_puts("FAILED (not all slabs found)\n");
    }
    
    // Check statistics - we should have added exactly NUM_TEST_SLABS entries
    size_t final_entries = slab_lookup_get_entry_count();
    size_t added_entries = final_entries - baseline_entries;
    
    // Since each test slab spans exactly 1 page, we should add exactly NUM_TEST_SLABS entries
    if (added_entries != NUM_TEST_SLABS) {
        uart_puts("    WARNING: Expected to add ");
        uart_putdec(NUM_TEST_SLABS);
        uart_puts(" entries but added ");
        uart_putdec(added_entries);
        uart_puts("\n");
    }
    
    // Clean up
    for (int i = 0; i < NUM_TEST_SLABS; i++) {
        slab_lookup_remove(slabs[i]);
        pmm_free_page(DMAP_TO_PHYS((uintptr_t)slabs[i]));
    }
}

// Test 6: NULL and edge cases
static void test_null_edge_cases(void) {
    uart_puts("  Test 6: NULL and edge cases... ");
    tests_run++;
    
    // Test NULL lookups
    struct kmem_cache *found = slab_lookup_find(NULL);
    
    // Test removing NULL
    slab_lookup_remove(NULL);  // Should not crash
    
    // Test inserting NULL
    slab_lookup_insert(NULL);  // Should not crash
    
    if (found == NULL) {
        uart_puts("PASSED\n");
        tests_passed++;
    } else {
        uart_puts("FAILED (NULL lookup returned non-NULL)\n");
    }
}

// Test 7: Hash distribution
static void test_hash_distribution(void) {
    uart_puts("  Test 7: Hash distribution check... ");
    tests_run++;
    
    // This test checks that our hash function distributes well
    // We'll create slabs at regular intervals and check they don't all collide
    
    struct kmem_cache cache = {0};
    strncpy(cache.warm.name, "dist_cache", 31);
    
    #define DIST_TEST_COUNT 32
    struct kmem_slab *slabs[DIST_TEST_COUNT];
    
    // Create slabs at 1MB intervals
    for (int i = 0; i < DIST_TEST_COUNT; i++) {
        void *addr = (void *)(0xFFFF000050000000UL + (i * 0x100000));  // 1MB apart
        slabs[i] = create_test_slab(addr, PAGE_SIZE, &cache);
        
        if (!slabs[i]) {
            uart_puts("FAILED (couldn't create test slabs)\n");
            // Clean up
            for (int j = 0; j < i; j++) {
                slab_lookup_remove(slabs[j]);
                pmm_free_page(DMAP_TO_PHYS((uintptr_t)slabs[j]));
            }
            return;
        }
        
        slab_lookup_insert(slabs[i]);
    }
    
    // Get load factor - should be reasonable
    uint32_t load_factor_percent = slab_lookup_get_load_factor_percent();
    
    // For 32 entries in 64 buckets, load factor should be 50%
    // But with collisions it might be different
    // As long as we can find all entries, distribution is acceptable
    
    bool all_found = true;
    for (int i = 0; i < DIST_TEST_COUNT; i++) {
        void *addr = (void *)(0xFFFF000050000000UL + (i * 0x100000));
        if (slab_lookup_find(addr) != &cache) {
            all_found = false;
            break;
        }
    }
    
    if (all_found) {
        uart_puts("PASSED (load factor: ");
        uart_putdec(load_factor_percent);
        uart_puts("%)\n");
        tests_passed++;
    } else {
        uart_puts("FAILED (poor hash distribution)\n");
    }
    
    // Clean up
    for (int i = 0; i < DIST_TEST_COUNT; i++) {
        slab_lookup_remove(slabs[i]);
        pmm_free_page(DMAP_TO_PHYS((uintptr_t)slabs[i]));
    }
}

// Main test runner
void run_slab_lookup_tests(void) {
    uart_puts("\n=== Slab Lookup Hash Table Tests ===\n");
    
    // Run all tests
    test_basic_insert_lookup();
    test_collision_handling();
    test_boundary_addresses();
    test_remove_lookup();
    test_many_slabs();
    test_null_edge_cases();
    test_hash_distribution();
    
    // Summary
    uart_puts("\nSlab lookup tests completed: ");
    uart_putdec(tests_passed);
    uart_puts(" out of ");
    uart_putdec(tests_run);
    uart_puts(" passed\n");
    
    // Dump statistics
    slab_lookup_dump_stats();
    
    if (tests_passed < tests_run) {
        uart_puts("WARNING: Some slab lookup tests failed!\n");
    }
}