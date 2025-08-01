/*
 * kernel/tests/slab_tests.c
 *
 * Unit tests for the slab allocator
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
    uart_puts("\n[TEST] "); \
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

// Helper to format size name
static void format_size_name(char *str, size_t size) {
    strcpy(str, "test_size_");
    int pos = 10;
    if (size >= 1000) str[pos++] = '0' + (size / 1000) % 10;
    if (size >= 100) str[pos++] = '0' + (size / 100) % 10;
    if (size >= 10) str[pos++] = '0' + (size / 10) % 10;
    str[pos++] = '0' + size % 10;
    str[pos] = '\0';
}

// Test basic cache creation and destruction
static int test_cache_create_destroy(void) {
    TEST_START("test_cache_create_destroy");
    
    // Create a cache for 64-byte objects
    struct kmem_cache *cache = kmem_cache_create("test_cache", 64, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Verify cache properties
    ASSERT(cache->object_size == 64, "Invalid object size");
    ASSERT(cache->align == 8, "Invalid alignment");
    ASSERT(strcmp(cache->name, "test_cache") == 0, "Invalid cache name");
    
    // Destroy the cache
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test single object allocation and free
static int test_single_alloc_free(void) {
    TEST_START("test_single_alloc_free");
    
    struct kmem_cache *cache = kmem_cache_create("test_single", 128, 16, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Allocate an object
    void *obj = kmem_cache_alloc(cache, KM_NOSLEEP);
    ASSERT(obj != NULL, "Failed to allocate object");
    
    // Verify alignment
    ASSERT(((uintptr_t)obj & 15) == 0, "Object not properly aligned");
    
    // Write pattern to object
    memset(obj, 0xAB, 128);
    
    // Free the object
    kmem_cache_free(cache, obj);
    
    // Clean up
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test multiple allocations
static int test_multiple_alloc(void) {
    TEST_START("test_multiple_alloc");
    
    struct kmem_cache *cache = kmem_cache_create("test_multi", 32, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Allocate multiple objects
    void *objs[50];
    for (int i = 0; i < 50; i++) {
        objs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(objs[i] != NULL, "Failed to allocate object");
        
        // Write unique pattern
        memset(objs[i], i, 32);
    }
    
    // Verify objects are distinct
    for (int i = 0; i < 50; i++) {
        for (int j = i + 1; j < 50; j++) {
            ASSERT(objs[i] != objs[j], "Duplicate object pointers");
        }
    }
    
    // Verify patterns
    for (int i = 0; i < 50; i++) {
        unsigned char *p = (unsigned char *)objs[i];
        for (int j = 0; j < 32; j++) {
            ASSERT(p[j] == i, "Pattern corrupted");
        }
    }
    
    // Free all objects
    for (int i = 0; i < 50; i++) {
        kmem_cache_free(cache, objs[i]);
    }
    
    // Clean up
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test allocation with KM_ZERO flag
static int test_alloc_zero(void) {
    TEST_START("test_alloc_zero");
    
    struct kmem_cache *cache = kmem_cache_create("test_zero", 256, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    // Allocate with KM_ZERO
    void *obj = kmem_cache_alloc(cache, KM_NOSLEEP | KM_ZERO);
    ASSERT(obj != NULL, "Failed to allocate object");
    
    // Verify memory is zeroed
    unsigned char *p = (unsigned char *)obj;
    for (int i = 0; i < 256; i++) {
        ASSERT(p[i] == 0, "Memory not zeroed");
    }
    
    kmem_cache_free(cache, obj);
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test cache statistics
static int test_cache_stats(void) {
    TEST_START("test_cache_stats");
    
    struct kmem_cache *cache = kmem_cache_create("test_stats", 64, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    struct kmem_stats stats;
    kmem_cache_stats(cache, &stats);
    
    // Initial stats
    ASSERT(stats.allocs == 0, "Initial allocs should be 0");
    ASSERT(stats.frees == 0, "Initial frees should be 0");
    ASSERT(stats.active_objs == 0, "Initial active_objs should be 0");
    
    // Allocate some objects
    void *obj1 = kmem_cache_alloc(cache, KM_NOSLEEP);
    void *obj2 = kmem_cache_alloc(cache, KM_NOSLEEP);
    void *obj3 = kmem_cache_alloc(cache, KM_NOSLEEP);
    
    kmem_cache_stats(cache, &stats);
    ASSERT(stats.allocs == 3, "Allocs should be 3");
    ASSERT(stats.active_objs == 3, "Active objects should be 3");
    
    // Free one object
    kmem_cache_free(cache, obj2);
    
    kmem_cache_stats(cache, &stats);
    ASSERT(stats.frees == 1, "Frees should be 1");
    ASSERT(stats.active_objs == 2, "Active objects should be 2");
    
    // Clean up
    kmem_cache_free(cache, obj1);
    kmem_cache_free(cache, obj3);
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test allocation/free patterns
static int test_alloc_free_patterns(void) {
    TEST_START("test_alloc_free_patterns");
    
    struct kmem_cache *cache = kmem_cache_create("test_patterns", 48, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    void *objs[20];
    
    // Pattern 1: Allocate all, free all
    for (int i = 0; i < 20; i++) {
        objs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(objs[i] != NULL, "Failed to allocate");
    }
    for (int i = 0; i < 20; i++) {
        kmem_cache_free(cache, objs[i]);
    }
    
    // Pattern 2: Allocate and free alternating
    for (int i = 0; i < 20; i++) {
        objs[i] = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(objs[i] != NULL, "Failed to allocate");
        if (i % 2 == 1) {
            kmem_cache_free(cache, objs[i-1]);
            kmem_cache_free(cache, objs[i]);
        }
    }
    
    // Free remaining (only even-indexed objects that weren't freed)
    for (int i = 0; i < 18; i += 2) {
        kmem_cache_free(cache, objs[i]);
    }
    
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Test different object sizes
static int test_various_sizes(void) {
    TEST_START("test_various_sizes");
    
    size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (int i = 0; i < num_sizes; i++) {
        char name[32];
        format_size_name(name, sizes[i]);
        
        struct kmem_cache *cache = kmem_cache_create(name, sizes[i], 8, 0);
        ASSERT(cache != NULL, "Failed to create cache");
        
        // Allocate a few objects
        void *obj1 = kmem_cache_alloc(cache, KM_NOSLEEP);
        void *obj2 = kmem_cache_alloc(cache, KM_NOSLEEP);
        ASSERT(obj1 != NULL && obj2 != NULL, "Failed to allocate");
        
        // Verify they don't overlap
        ASSERT((char *)obj2 - (char *)obj1 >= (long)sizes[i] ||
               (char *)obj1 - (char *)obj2 >= (long)sizes[i], 
               "Objects overlap");
        
        kmem_cache_free(cache, obj1);
        kmem_cache_free(cache, obj2);
        kmem_cache_destroy(cache);
    }
    
    TEST_PASS();
    return 1;
}

// Test slab reuse after free
static int test_slab_reuse(void) {
    TEST_START("test_slab_reuse");
    
    struct kmem_cache *cache = kmem_cache_create("test_reuse", 64, 8, 0);
    ASSERT(cache != NULL, "Failed to create cache");
    
    void *first_obj = kmem_cache_alloc(cache, KM_NOSLEEP);
    ASSERT(first_obj != NULL, "Failed to allocate first object");
    
    // Write pattern
    memset(first_obj, 0x55, 64);
    
    // Free it
    kmem_cache_free(cache, first_obj);
    
    // Allocate again - should get the same object
    void *second_obj = kmem_cache_alloc(cache, KM_NOSLEEP);
    ASSERT(second_obj != NULL, "Failed to allocate second object");
    ASSERT(second_obj == first_obj, "Should reuse freed object");
    
    kmem_cache_free(cache, second_obj);
    kmem_cache_destroy(cache);
    
    TEST_PASS();
    return 1;
}

// Main test runner
void run_slab_tests(void) {
    uart_puts("\n=== Starting Slab Allocator Tests ===\n");
    
    // Initialize slab allocator
    kmem_init();
    
    // Run tests
    test_cache_create_destroy();
    test_single_alloc_free();
    test_multiple_alloc();
    test_alloc_zero();
    test_cache_stats();
    test_alloc_free_patterns();
    test_various_sizes();
    test_slab_reuse();
    
    // Summary
    uart_puts("\n=== Test Summary ===\n");
    uart_puts("Tests run: ");
    uart_putdec(tests_run);
    uart_puts("\nTests passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nTests failed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("\nAll tests PASSED!\n");
    } else {
        uart_puts("\nSome tests FAILED!\n");
    }
    
    // Dump cache info for debugging
    uart_puts("\n=== Final Cache State ===\n");
    kmem_dump_all_caches();
}