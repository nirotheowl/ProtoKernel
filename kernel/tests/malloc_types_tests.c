/*
 * kernel/tests/malloc_types_tests.c
 *
 * Unit tests for the malloc type system
 */

#include <memory/malloc_types.h>
#include <memory/kmalloc.h>
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

// Define test malloc types
MALLOC_DEFINE(M_TEST1, "test1", "Test type 1");
MALLOC_DEFINE(M_TEST2, "test2", "Test type 2");
MALLOC_DEFINE(M_DYNAMIC, "dynamic", "Dynamically allocated test type");

// Test basic type registration
static int test_type_registration(void) {
    TEST_START("Type registration");
    
    // Register test types
    malloc_type_register(&M_TEST1);
    malloc_type_register(&M_TEST2);
    
    // Find registered types
    struct malloc_type *type1 = malloc_type_find("test1");
    struct malloc_type *type2 = malloc_type_find("test2");
    
    ASSERT(type1 == &M_TEST1, "Failed to find test1 type");
    ASSERT(type2 == &M_TEST2, "Failed to find test2 type");
    
    // Test non-existent type
    struct malloc_type *type3 = malloc_type_find("nonexistent");
    ASSERT(type3 == NULL, "Found non-existent type");
    
    TEST_PASS();
    return 1;
}

// Test duplicate registration
static int test_duplicate_registration(void) {
    TEST_START("Duplicate registration handling");
    
    // Try to register same type twice
    malloc_type_register(&M_TEST1);  // Should be a no-op
    
    // Verify it's still there and stats haven't been reset
    struct malloc_type *type = malloc_type_find("test1");
    ASSERT(type == &M_TEST1, "Type not found after duplicate registration");
    
    TEST_PASS();
    return 1;
}

// Test allocation statistics
static int test_allocation_stats(void) {
    TEST_START("Allocation statistics");
    
    // Get initial stats
    struct malloc_type_stats initial_stats;
    malloc_type_get_stats(&M_TEST1, &initial_stats);
    
    // Make some allocations
    void *ptr1 = kmalloc_type(128, &M_TEST1, KM_NOSLEEP);
    void *ptr2 = kmalloc_type(256, &M_TEST1, KM_NOSLEEP);
    void *ptr3 = kmalloc_type(64, &M_TEST1, KM_NOSLEEP);
    
    ASSERT(ptr1 != NULL, "Allocation 1 failed");
    ASSERT(ptr2 != NULL, "Allocation 2 failed");
    ASSERT(ptr3 != NULL, "Allocation 3 failed");
    
    // Check statistics
    struct malloc_type_stats stats;
    malloc_type_get_stats(&M_TEST1, &stats);
    
    ASSERT(stats.allocs == initial_stats.allocs + 3, "Incorrect allocation count");
    ASSERT(stats.current_allocs == initial_stats.current_allocs + 3, "Incorrect current allocation count");
    ASSERT(stats.bytes_allocated >= initial_stats.bytes_allocated + 448, "Incorrect bytes allocated");
    ASSERT(stats.current_bytes >= initial_stats.current_bytes + 448, "Incorrect current bytes");
    
    // Free one allocation
    kfree_type(ptr2, &M_TEST1);
    
    // Check stats after free
    malloc_type_get_stats(&M_TEST1, &stats);
    ASSERT(stats.frees == initial_stats.frees + 1, "Incorrect free count");
    ASSERT(stats.current_allocs == initial_stats.current_allocs + 2, "Incorrect current allocation count after free");
    
    // Free remaining allocations
    kfree_type(ptr1, &M_TEST1);
    kfree_type(ptr3, &M_TEST1);
    
    TEST_PASS();
    return 1;
}

// Test peak tracking
static int test_peak_tracking(void) {
    TEST_START("Peak statistics tracking");
    
    // Get initial peaks
    uint64_t initial_peak_allocs = M_TEST2.stats.peak_allocs;
    uint64_t initial_peak_bytes = M_TEST2.stats.peak_bytes;
    
    // Make allocations
    void *ptrs[5];
    for (int i = 0; i < 5; i++) {
        ptrs[i] = kmalloc_type(100 * (i + 1), &M_TEST2, KM_NOSLEEP);
        ASSERT(ptrs[i] != NULL, "Allocation failed");
    }
    
    // Check peaks
    ASSERT(M_TEST2.stats.peak_allocs >= initial_peak_allocs + 5, "Peak allocations not updated");
    ASSERT(M_TEST2.stats.peak_bytes >= initial_peak_bytes + 1500, "Peak bytes not updated");
    
    // Free some allocations
    kfree_type(ptrs[0], &M_TEST2);
    kfree_type(ptrs[2], &M_TEST2);
    kfree_type(ptrs[4], &M_TEST2);
    
    // Peaks should remain unchanged
    uint64_t peak_after_free = M_TEST2.stats.peak_allocs;
    ASSERT(peak_after_free >= initial_peak_allocs + 5, "Peak allocations decreased after free");
    
    // Free remaining
    kfree_type(ptrs[1], &M_TEST2);
    kfree_type(ptrs[3], &M_TEST2);
    
    TEST_PASS();
    return 1;
}

// Test type iteration
static int test_type_iteration(void) {
    TEST_START("Type iteration");
    
    int count = 0;
    int found_test1 = 0;
    int found_test2 = 0;
    int found_kmalloc = 0;
    
    struct malloc_type *type = NULL;
    while ((type = malloc_type_iterate(type)) != NULL) {
        count++;
        if (strcmp(type->name, "test1") == 0) found_test1 = 1;
        if (strcmp(type->name, "test2") == 0) found_test2 = 1;
        if (strcmp(type->name, "kmalloc") == 0) found_kmalloc = 1;
    }
    
    ASSERT(count > 0, "No types found during iteration");
    ASSERT(found_test1, "test1 type not found during iteration");
    ASSERT(found_test2, "test2 type not found during iteration");
    ASSERT(found_kmalloc, "kmalloc type not found during iteration");
    
    TEST_PASS();
    return 1;
}

// Test with NULL type (should use default M_KMALLOC)
static int test_null_type(void) {
    TEST_START("NULL type handling");
    
    // Get initial M_KMALLOC stats
    struct malloc_type_stats initial_stats;
    malloc_type_get_stats(&M_KMALLOC, &initial_stats);
    
    // Allocate with NULL type
    void *ptr = kmalloc_type(512, NULL, KM_NOSLEEP);
    ASSERT(ptr != NULL, "Allocation with NULL type failed");
    
    // Check that M_KMALLOC stats were updated
    struct malloc_type_stats stats;
    malloc_type_get_stats(&M_KMALLOC, &stats);
    ASSERT(stats.allocs == initial_stats.allocs + 1, "M_KMALLOC stats not updated for NULL type");
    
    // Free with NULL type
    kfree_type(ptr, NULL);
    
    TEST_PASS();
    return 1;
}

// Test unregistration
static int test_type_unregistration(void) {
    TEST_START("Type unregistration");
    
    // Register dynamic type
    malloc_type_register(&M_DYNAMIC);
    
    // Verify it's registered
    struct malloc_type *type = malloc_type_find("dynamic");
    ASSERT(type == &M_DYNAMIC, "Dynamic type not found after registration");
    
    // Unregister it
    malloc_type_unregister(&M_DYNAMIC);
    
    // Verify it's gone
    type = malloc_type_find("dynamic");
    ASSERT(type == NULL, "Dynamic type still found after unregistration");
    
    TEST_PASS();
    return 1;
}

// Test large allocation type tracking
static int test_large_allocation_types(void) {
    TEST_START("Large allocation type tracking");
    
    // Allocate something larger than 64KB
    size_t large_size = 128 * 1024;  // 128KB
    void *ptr = kmalloc_type(large_size, &M_TEST1, KM_NOSLEEP);
    
    if (ptr == NULL) {
        uart_puts("SKIP (large allocation not supported)\n");
        return 1;
    }
    
    // Check that stats were updated
    ASSERT(M_TEST1.stats.current_bytes >= large_size, "Large allocation not tracked in type stats");
    
    // Free it
    kfree_type(ptr, &M_TEST1);
    
    TEST_PASS();
    return 1;
}

// Run all malloc type tests
void run_malloc_types_tests(void) {
    uart_puts("\n=== Malloc Type System Tests ===\n");
    
    // Run tests
    test_type_registration();
    test_duplicate_registration();
    test_allocation_stats();
    test_peak_tracking();
    test_type_iteration();
    test_null_type();
    test_type_unregistration();
    test_large_allocation_types();
    
    // Print summary
    uart_puts("\n=== Test Summary ===\n");
    uart_puts("Total tests: ");
    uart_putdec(tests_run);
    uart_puts("\nPassed: ");
    uart_putdec(tests_passed);
    uart_puts("\nFailed: ");
    uart_putdec(tests_failed);
    uart_puts("\n\n");
    
    // Optional: Dump type statistics
    if (tests_passed > 0) {
        uart_puts("=== Type Statistics After Tests ===\n");
        malloc_type_dump_stats();
    }
}