#include <irq/irq.h>
#include <irq/irq_alloc.h>
#include <uart.h>
#include <string.h>

// Test results tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        uart_puts("[FAIL] "); \
        uart_puts(__func__); \
        uart_puts(": "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    uart_puts("[PASS] "); \
    uart_puts(__func__); \
    uart_puts(": "); \
    uart_puts(msg); \
    uart_puts("\n"); \
    tests_passed++; \
} while(0)

// Test basic single allocation
static void test_single_allocation(void) {
    uint32_t virq;
    
    virq = virq_alloc();
    TEST_ASSERT(virq != IRQ_INVALID, "Should allocate valid virq");
    TEST_ASSERT(virq > 0, "virq should be > 0 (0 is reserved)");
    TEST_ASSERT(virq_is_allocated(virq), "virq should be marked as allocated");
    TEST_PASS("Single allocation");
    
    virq_free(virq);
    TEST_ASSERT(!virq_is_allocated(virq), "virq should be freed");
    TEST_PASS("Single free");
}

// Test multiple sequential allocations
static void test_sequential_allocation(void) {
    uint32_t virqs[10];
    int i;
    
    // Allocate 10 virqs
    for (i = 0; i < 10; i++) {
        virqs[i] = virq_alloc();
        TEST_ASSERT(virqs[i] != IRQ_INVALID, "Sequential allocation should succeed");
        TEST_ASSERT(virq_is_allocated(virqs[i]), "Allocated virq should be marked");
    }
    TEST_PASS("10 sequential allocations");
    
    // Verify all are unique
    for (i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            TEST_ASSERT(virqs[i] != virqs[j], "virqs should be unique");
        }
    }
    TEST_PASS("All virqs unique");
    
    // Free them all
    for (i = 0; i < 10; i++) {
        virq_free(virqs[i]);
        TEST_ASSERT(!virq_is_allocated(virqs[i]), "Freed virq should not be allocated");
    }
    TEST_PASS("All virqs freed");
}

// Test range allocation
static void test_range_allocation(void) {
    uint32_t base;
    int i;
    
    // Allocate range of 8
    base = virq_alloc_range(8);
    TEST_ASSERT(base != IRQ_INVALID, "Range allocation should succeed");
    
    // Verify all 8 are allocated
    for (i = 0; i < 8; i++) {
        TEST_ASSERT(virq_is_allocated(base + i), "Range virq should be allocated");
    }
    TEST_PASS("Range of 8 allocated");
    
    // Try to allocate one in the middle (should fail since already allocated)
    virq_free(base + 4);  // Free one in middle
    uint32_t mid = virq_alloc();
    TEST_ASSERT(mid == base + 4, "Should reuse freed virq in range");
    TEST_PASS("Reuse freed virq in range");
    
    // Free the range (except the one we just allocated)
    virq_free_range(base, 4);
    virq_free_range(base + 5, 3);
    virq_free(mid);
    
    // Verify all are freed
    for (i = 0; i < 8; i++) {
        TEST_ASSERT(!virq_is_allocated(base + i), "Range should be freed");
    }
    TEST_PASS("Range freed");
}

// Test fragmentation and coalescence
static void test_fragmentation(void) {
    uint32_t virqs[20];
    int i;
    
    // Allocate 20 virqs
    for (i = 0; i < 20; i++) {
        virqs[i] = virq_alloc();
        TEST_ASSERT(virqs[i] != IRQ_INVALID, "Allocation should succeed");
    }
    
    // Free every other one
    for (i = 0; i < 20; i += 2) {
        virq_free(virqs[i]);
    }
    TEST_PASS("Created fragmented allocation");
    
    // Try to allocate a range of 2 - should fail in fragmented region
    // but might succeed if it finds space elsewhere
    uint32_t range = virq_alloc_range(2);
    if (range != IRQ_INVALID) {
        // Should be outside the fragmented region or at the end
        TEST_ASSERT(range > virqs[19] || range < virqs[0], 
                   "Range should be outside fragmented region");
        virq_free_range(range, 2);
    }
    TEST_PASS("Range allocation in fragmented space handled");
    
    // Clean up
    for (i = 1; i < 20; i += 2) {
        virq_free(virqs[i]);
    }
}

// Test allocation limits
static void test_allocation_limits(void) {
    uint32_t count = virq_get_allocated_count();
    uint32_t initial_count = count;
    uint32_t virqs[100];
    int allocated = 0;
    int i;
    
    // Try to allocate many virqs
    for (i = 0; i < 100; i++) {
        virqs[i] = virq_alloc();
        if (virqs[i] == IRQ_INVALID) {
            break;
        }
        allocated++;
    }
    
    TEST_ASSERT(allocated > 0, "Should allocate at least some virqs");
    TEST_PASS("Allocated virqs until limit");
    
    // Check count increased correctly
    count = virq_get_allocated_count();
    TEST_ASSERT(count == initial_count + allocated, "Count should match allocations");
    TEST_PASS("Allocation count correct");
    
    // Free them all
    for (i = 0; i < allocated; i++) {
        virq_free(virqs[i]);
    }
    
    count = virq_get_allocated_count();
    TEST_ASSERT(count == initial_count, "Count should return to initial");
    TEST_PASS("Count restored after free");
}

// Test invalid operations
static void test_invalid_operations(void) {
    // Test freeing invalid virqs
    virq_free(0);  // Reserved virq
    virq_free(IRQ_INVALID);
    virq_free(99999);  // Out of range
    TEST_PASS("Invalid free operations handled");
    
    // Test invalid range allocations
    uint32_t virq = virq_alloc_range(0);
    TEST_ASSERT(virq == IRQ_INVALID, "Zero range should fail");
    
    virq = virq_alloc_range(10000);  // Too large
    TEST_ASSERT(virq == IRQ_INVALID, "Huge range should fail");
    TEST_PASS("Invalid range allocations rejected");
    
    // Test invalid range free
    virq_free_range(0, 10);  // Starting from reserved
    virq_free_range(IRQ_INVALID, 5);
    virq_free_range(99999, 10);  // Out of range
    TEST_PASS("Invalid range free operations handled");
}

// Test allocation patterns
static void test_allocation_patterns(void) {
    uint32_t singles[5];
    uint32_t ranges[3];
    int i;
    
    // Mix single and range allocations
    singles[0] = virq_alloc();
    ranges[0] = virq_alloc_range(4);
    singles[1] = virq_alloc();
    ranges[1] = virq_alloc_range(8);
    singles[2] = virq_alloc();
    singles[3] = virq_alloc();
    ranges[2] = virq_alloc_range(3);
    singles[4] = virq_alloc();
    
    // Verify all succeeded
    for (i = 0; i < 5; i++) {
        TEST_ASSERT(singles[i] != IRQ_INVALID, "Single allocation should succeed");
    }
    for (i = 0; i < 3; i++) {
        TEST_ASSERT(ranges[i] != IRQ_INVALID, "Range allocation should succeed");
    }
    TEST_PASS("Mixed allocation pattern succeeded");
    
    // Free in different order
    virq_free_range(ranges[1], 8);
    virq_free(singles[2]);
    virq_free_range(ranges[0], 4);
    virq_free(singles[0]);
    virq_free(singles[4]);
    virq_free_range(ranges[2], 3);
    virq_free(singles[1]);
    virq_free(singles[3]);
    
    TEST_PASS("Mixed free pattern succeeded");
}

// Test reallocation behavior
static void test_reallocation(void) {
    uint32_t virq1, virq2, virq3;
    
    // Allocate and free repeatedly
    virq1 = virq_alloc();
    TEST_ASSERT(virq1 != IRQ_INVALID, "First allocation");
    
    virq_free(virq1);
    
    virq2 = virq_alloc();
    TEST_ASSERT(virq2 == virq1, "Should reuse recently freed virq");
    TEST_PASS("Immediate reallocation");
    
    // Allocate another, free first, then allocate
    virq3 = virq_alloc();
    virq_free(virq2);
    virq1 = virq_alloc();
    TEST_ASSERT(virq1 == virq2, "Should reuse lowest freed virq");
    TEST_PASS("Lowest virq reallocation");
    
    virq_free(virq1);
    virq_free(virq3);
}

// Test statistics functions
static void test_statistics(void) {
    uint32_t initial_count = virq_get_allocated_count();
    uint32_t initial_max = virq_get_max_allocated();
    uint32_t virqs[5];
    int i;
    
    // Allocate some virqs
    for (i = 0; i < 5; i++) {
        virqs[i] = virq_alloc();
    }
    
    TEST_ASSERT(virq_get_allocated_count() == initial_count + 5, 
               "Allocated count should increase by 5");
    TEST_ASSERT(virq_get_max_allocated() >= virqs[4], 
               "Max allocated should be at least last virq");
    TEST_PASS("Statistics updated correctly");
    
    // Free some
    virq_free(virqs[2]);
    virq_free(virqs[3]);
    
    TEST_ASSERT(virq_get_allocated_count() == initial_count + 3,
               "Count should decrease after free");
    TEST_ASSERT(virq_get_max_allocated() == virqs[4],
               "Max should remain highest allocated");
    TEST_PASS("Statistics after partial free");
    
    // Free the rest
    virq_free(virqs[0]);
    virq_free(virqs[1]);
    virq_free(virqs[4]);
    
    TEST_ASSERT(virq_get_allocated_count() == initial_count,
               "Count should return to initial");
    TEST_PASS("Statistics restored");
}

// Main test runner for allocator
void run_irq_allocator_tests(void) {
    uart_puts("\n========================================\n");
    uart_puts("   IRQ ALLOCATOR COMPREHENSIVE TESTS\n");
    uart_puts("========================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Initialize allocator
    virq_allocator_init();
    
    // Run all tests
    test_single_allocation();
    test_sequential_allocation();
    test_range_allocation();
    test_fragmentation();
    test_allocation_limits();
    test_invalid_operations();
    test_allocation_patterns();
    test_reallocation();
    test_statistics();
    
    // Summary
    uart_puts("\nAllocator Test Summary:\n");
    uart_puts("  PASSED: ");
    uart_putdec(tests_passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
}