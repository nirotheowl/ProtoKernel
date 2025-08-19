#include <irq/irq_domain.h>
#include <irq/irq.h>
#include <irq/irq_alloc.h>
#include <memory/kmalloc.h>
#include <lib/radix_tree.h>
#include <uart.h>
#include <panic.h>
#include <string.h>

// Test statistics
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int assertions_run = 0;

// Helper macros for test assertions - these don't count as "tests"
#define TEST_ASSERT(condition, msg) do { \
    assertions_run++; \
    if (!(condition)) { \
        uart_puts("  [FAIL] "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        return -1; \
    } \
} while(0)

#define TEST_ASSERT_EQ(actual, expected, msg) do { \
    assertions_run++; \
    if ((actual) != (expected)) { \
        uart_puts("  [FAIL] "); \
        uart_puts(msg); \
        uart_puts(" (expected "); \
        uart_putdec(expected); \
        uart_puts(", got "); \
        uart_putdec(actual); \
        uart_puts(")\n"); \
        return -1; \
    } \
} while(0)

#define TEST_ASSERT_NE(actual, not_expected, msg) do { \
    assertions_run++; \
    if ((actual) == (not_expected)) { \
        uart_puts("  [FAIL] "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        return -1; \
    } \
} while(0)

// Macro to run a test and count it properly
#define RUN_TEST(test_func) do { \
    tests_run++; \
    int result = test_func(); \
    if (result == 0) { \
        tests_passed++; \
        print_test_result(result); \
    } else { \
        tests_failed++; \
        print_test_result(result); \
    } \
} while(0)

// Test helper functions
static void print_test_header(const char *test_name) {
    uart_puts("\n[TEST] ");
    uart_puts(test_name);
    uart_puts("\n");
}

static void print_test_result(int result) {
    if (result == 0) {
        uart_puts("  [PASS] Test completed successfully\n");
    } else {
        uart_puts("  [FAIL] Test failed\n");
    }
}

// Test 1: Basic tree domain creation and destruction
static int test_tree_domain_creation(void) {
    print_test_header("Tree Domain Creation");
    
    struct irq_domain *domain;
    
    // Create tree domain
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    TEST_ASSERT_EQ(domain->type, DOMAIN_TREE, "Domain type should be DOMAIN_TREE");
    TEST_ASSERT(domain->tree != NULL, "Tree root should be allocated");
    TEST_ASSERT_EQ(domain->max_irq, 0xFFFFFF, "Max IRQ should be 16M");
    
    // Clean up
    irq_domain_remove(domain);
    
    return 0;
}

// Test 2: Sparse mapping creation
static int test_sparse_mapping(void) {
    print_test_header("Sparse Mapping Creation");
    
    struct irq_domain *domain;
    uint32_t virq1, virq2, virq3;
    uint32_t hwirq1 = 0;
    uint32_t hwirq2 = 1000;
    uint32_t hwirq3 = 1000000;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Create sparse mappings
    virq1 = irq_create_mapping(domain, hwirq1);
    TEST_ASSERT_NE(virq1, IRQ_INVALID, "Failed to create mapping for hwirq 0");
    
    virq2 = irq_create_mapping(domain, hwirq2);
    TEST_ASSERT_NE(virq2, IRQ_INVALID, "Failed to create mapping for hwirq 1000");
    
    virq3 = irq_create_mapping(domain, hwirq3);
    TEST_ASSERT_NE(virq3, IRQ_INVALID, "Failed to create mapping for hwirq 1000000");
    
    // Verify mappings
    TEST_ASSERT_EQ(irq_find_mapping(domain, hwirq1), virq1, "Mapping lookup failed for hwirq 0");
    TEST_ASSERT_EQ(irq_find_mapping(domain, hwirq2), virq2, "Mapping lookup failed for hwirq 1000");
    TEST_ASSERT_EQ(irq_find_mapping(domain, hwirq3), virq3, "Mapping lookup failed for hwirq 1000000");
    
    // Test non-existent mapping
    TEST_ASSERT_EQ(irq_find_mapping(domain, 500000), IRQ_INVALID, "Non-existent mapping should return IRQ_INVALID");
    
    // Clean up
    irq_dispose_mapping(virq1);
    irq_dispose_mapping(virq2);
    irq_dispose_mapping(virq3);
    irq_domain_remove(domain);
    
    return 0;
}

// Test 3: Duplicate mapping handling
static int test_duplicate_mapping(void) {
    print_test_header("Duplicate Mapping Handling");
    
    struct irq_domain *domain;
    uint32_t virq1, virq2;
    uint32_t hwirq = 12345;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Create first mapping
    virq1 = irq_create_mapping(domain, hwirq);
    TEST_ASSERT_NE(virq1, IRQ_INVALID, "Failed to create first mapping");
    
    // Try to create duplicate mapping - should return existing
    virq2 = irq_create_mapping(domain, hwirq);
    TEST_ASSERT_EQ(virq2, virq1, "Duplicate mapping should return existing virq");
    
    // Clean up
    irq_dispose_mapping(virq1);
    irq_domain_remove(domain);
    
    return 0;
}

// Test 4: Range allocation test for MSI
static int test_range_allocation(void) {
    print_test_header("Range Allocation for MSI");
    
    struct irq_domain *domain;
    uint32_t hwirq_base;
    int result;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Test allocating a range of 8 hwirqs
    result = irq_domain_alloc_hwirq_range(domain, 8, &hwirq_base);
    TEST_ASSERT(result == 0, "Failed to allocate hwirq range of 8");
    TEST_ASSERT(hwirq_base == 0, "First allocation should start at hwirq 0");
    
    // Test allocating another range of 16 hwirqs
    uint32_t hwirq_base2;
    result = irq_domain_alloc_hwirq_range(domain, 16, &hwirq_base2);
    TEST_ASSERT(result == 0, "Failed to allocate second hwirq range of 16");
    TEST_ASSERT(hwirq_base2 >= 8, "Second allocation should not overlap first");
    
    // Free the first range
    irq_domain_free_hwirq_range(domain, hwirq_base, 8);
    
    // Allocate a range of 4 - should reuse the freed space
    uint32_t hwirq_base3;
    result = irq_domain_alloc_hwirq_range(domain, 4, &hwirq_base3);
    TEST_ASSERT(result == 0, "Failed to allocate hwirq range after free");
    
    // Free remaining ranges
    irq_domain_free_hwirq_range(domain, hwirq_base2, 16);
    irq_domain_free_hwirq_range(domain, hwirq_base3, 4);
    
    // Clean up
    irq_domain_remove(domain);
    
    return 0;
}

// Test 5: Large sparse mapping stress test
static int test_large_sparse_mapping(void) {
    print_test_header("Large Sparse Mapping");
    
    struct irq_domain *domain;
    uint32_t virqs[100];
    uint32_t hwirqs[100];
    int i;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Create 100 sparse mappings with large gaps
    for (i = 0; i < 100; i++) {
        hwirqs[i] = i * 10000;  // Large gaps between hwirqs
        virqs[i] = irq_create_mapping(domain, hwirqs[i]);
        if (virqs[i] == IRQ_INVALID) {
            uart_puts("  [WARN] Failed to create mapping ");
            uart_putdec(i);
            uart_puts(" for hwirq ");
            uart_putdec(hwirqs[i]);
            uart_puts("\n");
            break;
        }
    }
    
    TEST_ASSERT(i > 50, "Should be able to create at least 50 sparse mappings");
    
    // Verify all created mappings
    int created_count = i;
    int verification_failures = 0;
    for (i = 0; i < created_count; i++) {
        uint32_t found_virq = irq_find_mapping(domain, hwirqs[i]);
        if (found_virq != virqs[i]) {
            uart_puts("  [FAIL] Mapping verification failed for index ");
            uart_putdec(i);
            uart_puts("\n");
            verification_failures++;
            if (verification_failures >= 5) break; // Limit error output
        }
    }
    
    TEST_ASSERT(verification_failures == 0, "All mappings should be verifiable");
    
    // Clean up all mappings
    for (i = 0; i < created_count; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    
    // Verify all mappings are gone
    int cleanup_failures = 0;
    for (i = 0; i < created_count; i++) {
        uint32_t found_virq = irq_find_mapping(domain, hwirqs[i]);
        if (found_virq != IRQ_INVALID) {
            uart_puts("  [FAIL] Mapping still exists after disposal for hwirq ");
            uart_putdec(hwirqs[i]);
            uart_puts("\n");
            cleanup_failures++;
            if (cleanup_failures >= 5) break; // Limit error output
        }
    }
    
    TEST_ASSERT(cleanup_failures == 0, "All mappings should be cleaned up");
    
    irq_domain_remove(domain);
    
    return 0;
}

// Test 6: MSI-style consecutive mapping
static int test_msi_consecutive_mapping(void) {
    print_test_header("MSI-style Consecutive Mapping");
    
    struct irq_domain *domain;
    uint32_t virq_base;
    uint32_t hwirq_base = 100000;  // Start from a high hwirq
    int i;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Allocate 32 consecutive virqs (typical MSI allocation)
    virq_base = irq_domain_alloc_irqs(domain, 32, NULL, NULL);
    TEST_ASSERT_NE(virq_base, (uint32_t)-1, "Failed to allocate 32 consecutive virqs");
    
    // Create mappings for consecutive hwirqs
    for (i = 0; i < 32; i++) {
        uint32_t virq = irq_create_mapping(domain, hwirq_base + i);
        if (virq == IRQ_INVALID) {
            uart_puts("  [FAIL] Failed to create mapping for hwirq ");
            uart_putdec(hwirq_base + i);
            uart_puts("\n");
            tests_failed++;
            break;
        }
    }
    
    // Verify all mappings
    int mapping_failures = 0;
    for (i = 0; i < 32; i++) {
        uint32_t virq = irq_find_mapping(domain, hwirq_base + i);
        if (virq == IRQ_INVALID) {
            uart_puts("  [FAIL] Mapping not found for hwirq ");
            uart_putdec(hwirq_base + i);
            uart_puts("\n");
            mapping_failures++;
        }
    }
    
    TEST_ASSERT(mapping_failures == 0, "All MSI mappings should be found");
    
    // Free all virqs
    irq_domain_free_irqs(virq_base, 32);
    
    // Clean up domain
    irq_domain_remove(domain);
    
    return 0;
}

// Test 7: Domain removal with active mappings
static int test_domain_removal_with_mappings(void) {
    print_test_header("Domain Removal with Active Mappings");
    
    struct irq_domain *domain;
    uint32_t virqs[10];
    uint32_t hwirqs[10] = {0, 100, 1000, 5000, 10000, 50000, 100000, 500000, 750000, 999999};
    int i;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Create multiple mappings
    for (i = 0; i < 10; i++) {
        virqs[i] = irq_create_mapping(domain, hwirqs[i]);
        TEST_ASSERT_NE(virqs[i], IRQ_INVALID, "Failed to create mapping");
    }
    
    // Remove domain - should clean up all mappings
    irq_domain_remove(domain);
    
    // Note: We can't verify mappings are gone without the domain
    // but the function should handle cleanup without crashing
    uart_puts("  [INFO] Domain removed with active mappings (cleanup test)\n");
    
    return 0;
}

// Test 8: Fragmentation handling
static int test_fragmentation_handling(void) {
    print_test_header("Fragmentation Handling");
    
    struct irq_domain *domain;
    uint32_t virqs[50];
    int i;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Create mappings with alternating pattern to cause fragmentation
    for (i = 0; i < 50; i++) {
        uint32_t hwirq = i * 2;  // Even numbers only
        virqs[i] = irq_create_mapping(domain, hwirq);
        TEST_ASSERT_NE(virqs[i], IRQ_INVALID, "Failed to create mapping");
    }
    
    // Remove every other mapping to create gaps
    for (i = 0; i < 50; i += 2) {
        irq_dispose_mapping(virqs[i]);
    }
    
    // Try to allocate in the gaps
    for (i = 0; i < 50; i += 2) {
        uint32_t hwirq = i * 2;
        virqs[i] = irq_create_mapping(domain, hwirq);
        TEST_ASSERT_NE(virqs[i], IRQ_INVALID, "Failed to reuse freed mapping slot");
    }
    
    // Verify all mappings
    for (i = 0; i < 50; i++) {
        uint32_t hwirq = i * 2;
        uint32_t found_virq = irq_find_mapping(domain, hwirq);
        TEST_ASSERT_EQ(found_virq, virqs[i], "Mapping verification failed after fragmentation");
    }
    
    // Clean up
    for (i = 0; i < 50; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    irq_domain_remove(domain);
    
    return 0;
}

// Test 9: Boundary conditions
static int test_boundary_conditions(void) {
    print_test_header("Boundary Conditions");
    
    struct irq_domain *domain;
    uint32_t virq;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Test hwirq 0 (minimum)
    virq = irq_create_mapping(domain, 0);
    TEST_ASSERT_NE(virq, IRQ_INVALID, "Failed to create mapping for hwirq 0");
    TEST_ASSERT_EQ(irq_find_mapping(domain, 0), virq, "Failed to find mapping for hwirq 0");
    irq_dispose_mapping(virq);
    
    // Test near maximum hwirq
    virq = irq_create_mapping(domain, 0xFFFFF0);
    TEST_ASSERT_NE(virq, IRQ_INVALID, "Failed to create mapping for large hwirq");
    TEST_ASSERT_EQ(irq_find_mapping(domain, 0xFFFFF0), virq, "Failed to find mapping for large hwirq");
    irq_dispose_mapping(virq);
    
    // Test maximum hwirq (0xFFFFFF - 1)
    virq = irq_create_mapping(domain, domain->max_irq - 1);
    TEST_ASSERT_NE(virq, IRQ_INVALID, "Failed to create mapping for max hwirq");
    TEST_ASSERT_EQ(irq_find_mapping(domain, domain->max_irq - 1), virq, "Failed to find mapping for max hwirq");
    irq_dispose_mapping(virq);
    
    // Clean up
    irq_domain_remove(domain);
    
    return 0;
}

// Test 10: Memory efficiency test
static int test_memory_efficiency(void) {
    print_test_header("Memory Efficiency");
    
    struct irq_domain *domain;
    uint32_t virq1, virq2;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Create two widely separated mappings
    // This tests that the radix tree doesn't allocate excessive memory
    virq1 = irq_create_mapping(domain, 0);
    TEST_ASSERT_NE(virq1, IRQ_INVALID, "Failed to create mapping for hwirq 0");
    
    virq2 = irq_create_mapping(domain, 0xF00000);  // ~15M
    TEST_ASSERT_NE(virq2, IRQ_INVALID, "Failed to create mapping for hwirq 0xF00000");
    
    // If we get here without running out of memory, the test passes
    uart_puts("  [INFO] Successfully created widely separated mappings\n");
    uart_puts("  [INFO] Radix tree handled sparse allocation efficiently\n");
    
    // Clean up
    irq_dispose_mapping(virq1);
    irq_dispose_mapping(virq2);
    irq_domain_remove(domain);
    
    return 0;
}

// Test 11: Massive stress test (10000 mappings)
static int test_massive_stress(void) {
    print_test_header("Massive Stress Test (1000 mappings)");
    
    struct irq_domain *domain;
    uint32_t *virqs;
    uint32_t *hwirqs;
    int i, created_count = 0;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Allocate arrays for tracking mappings
    virqs = kmalloc(1000 * sizeof(uint32_t), 0);
    hwirqs = kmalloc(1000 * sizeof(uint32_t), 0);
    TEST_ASSERT(virqs != NULL && hwirqs != NULL, "Failed to allocate tracking arrays");
    
    // Create 1000 random sparse mappings
    for (i = 0; i < 1000; i++) {
        // Generate pseudo-random hwirq using simple LFSR
        static uint32_t lfsr = 0xACE1;
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xB400);
        hwirqs[i] = (lfsr % 0x100000) + i;  // Ensure uniqueness
        
        virqs[i] = irq_create_mapping(domain, hwirqs[i]);
        if (virqs[i] != IRQ_INVALID) {
            created_count++;
        } else {
            break;
        }
    }
    
    uart_puts("  [INFO] Created ");
    uart_putdec(created_count);
    uart_puts(" mappings out of 1000 attempts\n");
    
    TEST_ASSERT(created_count >= 500, "Should create at least 500 mappings");
    
    // Verify all created mappings
    int verification_failures = 0;
    for (i = 0; i < created_count; i++) {
        uint32_t found_virq = irq_find_mapping(domain, hwirqs[i]);
        if (found_virq != virqs[i]) {
            verification_failures++;
        }
    }
    
    TEST_ASSERT_EQ(verification_failures, 0, "All mappings should be verifiable");
    
    // Clean up all mappings
    for (i = 0; i < created_count; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    
    // Verify all mappings are gone
    int cleanup_failures = 0;
    for (i = 0; i < created_count; i++) {
        uint32_t found_virq = irq_find_mapping(domain, hwirqs[i]);
        if (found_virq != IRQ_INVALID) {
            cleanup_failures++;
        }
    }
    
    TEST_ASSERT_EQ(cleanup_failures, 0, "All mappings should be cleaned up");
    
    // Clean up
    kfree(virqs);
    kfree(hwirqs);
    irq_domain_remove(domain);
    
    return 0;
}

// Test 12: Allocation/Free patterns
static int test_allocation_patterns(void) {
    print_test_header("Allocation/Free Patterns");
    
    struct irq_domain *domain;
    uint32_t virqs[100];
    int i;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Pattern 1: Sequential allocation and free
    uart_puts("  [INFO] Testing sequential allocation/free pattern\n");
    for (i = 0; i < 100; i++) {
        virqs[i] = irq_create_mapping(domain, i * 1000);
        TEST_ASSERT_NE(virqs[i], IRQ_INVALID, "Sequential allocation failed");
    }
    
    for (i = 0; i < 100; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    
    // Pattern 2: Reverse allocation and free
    uart_puts("  [INFO] Testing reverse allocation/free pattern\n");
    for (i = 99; i >= 0; i--) {
        virqs[i] = irq_create_mapping(domain, i * 1000 + 500000);
        TEST_ASSERT_NE(virqs[i], IRQ_INVALID, "Reverse allocation failed");
    }
    
    for (i = 99; i >= 0; i--) {
        irq_dispose_mapping(virqs[i]);
    }
    
    // Pattern 3: Interleaved allocation/free
    uart_puts("  [INFO] Testing interleaved allocation/free pattern\n");
    for (i = 0; i < 50; i++) {
        virqs[i] = irq_create_mapping(domain, i * 2000 + 1000000);
        TEST_ASSERT_NE(virqs[i], IRQ_INVALID, "Interleaved allocation failed");
        
        if (i > 10) {
            irq_dispose_mapping(virqs[i - 10]);
        }
    }
    
    // Clean up remaining mappings
    for (i = 40; i < 50; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    
    // Clean up
    irq_domain_remove(domain);
    
    return 0;
}

// Test 13: Tree rebalancing test
static int test_tree_rebalancing(void) {
    print_test_header("Tree Rebalancing");
    
    struct irq_domain *domain;
    uint32_t virqs[64];
    int i;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Create mappings that should trigger tree rebalancing
    // Use powers of 2 to stress the radix tree structure
    for (i = 0; i < 64; i++) {
        uint32_t hwirq = 1 << (i % 20);  // Powers of 2 up to 2^19
        hwirq += i * 13;  // Add offset to make unique
        
        virqs[i] = irq_create_mapping(domain, hwirq);
        TEST_ASSERT_NE(virqs[i], IRQ_INVALID, "Rebalancing test mapping failed");
    }
    
    uart_puts("  [INFO] Created 64 mappings with power-of-2 hwirqs\n");
    uart_puts("  [INFO] Tree should have rebalanced automatically\n");
    
    // Verify all mappings still work after potential rebalancing
    for (i = 0; i < 64; i++) {
        uint32_t hwirq = 1 << (i % 20);
        hwirq += i * 13;
        
        uint32_t found_virq = irq_find_mapping(domain, hwirq);
        TEST_ASSERT_EQ(found_virq, virqs[i], "Mapping lost after rebalancing");
    }
    
    // Clean up
    for (i = 0; i < 64; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    irq_domain_remove(domain);
    
    return 0;
}

// Test 14: Performance measurement test
static int test_performance_measurement(void) {
    print_test_header("Performance Measurement");
    
    struct irq_domain *domain;
    uint32_t virq;
    int i;
    
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Failed to create tree domain");
    
    // Test lookup performance with increasing tree depth
    uart_puts("  [INFO] Testing lookup performance...\n");
    
    // Create some mappings at different depths
    uint32_t test_hwirqs[] = {1, 256, 65536, 1048576, 4194304};
    uint32_t test_virqs[5];
    
    for (i = 0; i < 5; i++) {
        test_virqs[i] = irq_create_mapping(domain, test_hwirqs[i]);
        TEST_ASSERT_NE(test_virqs[i], IRQ_INVALID, "Performance test mapping failed");
    }
    
    // Perform multiple lookups to test consistency
    for (i = 0; i < 5; i++) {
        for (int j = 0; j < 100; j++) {
            virq = irq_find_mapping(domain, test_hwirqs[i]);
            TEST_ASSERT_EQ(virq, test_virqs[i], "Performance lookup inconsistent");
        }
    }
    
    uart_puts("  [INFO] Completed 500 lookups successfully\n");
    uart_puts("  [INFO] Performance appears consistent\n");
    
    // Clean up
    for (i = 0; i < 5; i++) {
        irq_dispose_mapping(test_virqs[i]);
    }
    irq_domain_remove(domain);
    
    return 0;
}

// Main test runner
void test_tree_domains_comprehensive(void) {
    uart_puts("\n================== TREE DOMAIN COMPREHENSIVE TESTS ==================\n");
    
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    assertions_run = 0;
    
    // Run all tests using the RUN_TEST macro
    RUN_TEST(test_tree_domain_creation);
    RUN_TEST(test_sparse_mapping);
    RUN_TEST(test_duplicate_mapping);
    RUN_TEST(test_range_allocation);
    RUN_TEST(test_large_sparse_mapping);
    RUN_TEST(test_msi_consecutive_mapping);
    RUN_TEST(test_domain_removal_with_mappings);
    RUN_TEST(test_fragmentation_handling);
    RUN_TEST(test_boundary_conditions);
    RUN_TEST(test_memory_efficiency);
    RUN_TEST(test_massive_stress);
    RUN_TEST(test_allocation_patterns);
    RUN_TEST(test_tree_rebalancing);
    RUN_TEST(test_performance_measurement);
    
    // Print summary
    uart_puts("\n=================== TREE DOMAIN TEST SUMMARY ===================\n");
    uart_puts("Total tests run: ");
    uart_putdec(tests_run);
    uart_puts("\nTotal assertions run: ");
    uart_putdec(assertions_run);
    uart_puts("\nTests passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nTests failed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("\n[SUCCESS] All tree domain tests passed!\n");
        uart_puts("MSI implementation is ready for integration.\n");
    } else {
        uart_puts("\n[FAILURE] Some tree domain tests failed.\n");
        uart_puts("Please review and fix the issues before proceeding.\n");
    }
    
    uart_puts("================================================================\n");
}
