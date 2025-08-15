#include <irq/irq.h>
#include <irq/irq_domain.h>
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

// Test handler
static int edge_handler_called = 0;
static void edge_handler(void *data) {
    (void)data;
    edge_handler_called++;
}

// Edge case: Zero and boundary values
static void test_boundary_values(void) {
    uint32_t virq;
    struct irq_desc *desc;
    int ret;
    
    // Test virq 0 (should be reserved/allocated)
    TEST_ASSERT(virq_is_allocated(0), "virq 0 is reserved");
    virq_free(0);  // Should handle gracefully - but shouldn't actually free it
    TEST_ASSERT(virq_is_allocated(0), "virq 0 stays reserved after free attempt");
    TEST_PASS("virq 0 handling");
    
    // Test maximum virq values
    virq_free(0xFFFFFFFF);  // Should handle gracefully
    virq_free_range(0xFFFFFFFF - 10, 20);  // Should handle gracefully
    TEST_PASS("Maximum virq handling");
    
    // Test zero-size operations
    virq = virq_alloc_range(0);
    TEST_ASSERT(virq == IRQ_INVALID, "Zero range rejected");
    
    virq_free_range(10, 0);  // Should handle gracefully
    TEST_PASS("Zero size operations");
    
    // Test NULL operations
    desc = irq_to_desc(IRQ_INVALID);
    TEST_ASSERT(desc == NULL, "Invalid virq returns NULL");
    
    ret = request_irq(5, NULL, 0, "null", NULL);
    TEST_ASSERT(ret != 0, "NULL handler rejected");
    
    free_irq(IRQ_INVALID, NULL);  // Should handle gracefully
    TEST_PASS("NULL pointer handling");
}

// Edge case: Overlapping operations
static void test_overlapping_operations(void) {
    uint32_t base1, base2;
    
    // Allocate a range
    base1 = virq_alloc_range(10);
    TEST_ASSERT(base1 != IRQ_INVALID, "First range allocated");
    
    // Free part of it
    virq_free_range(base1 + 3, 4);
    
    // Try to allocate overlapping range
    base2 = virq_alloc_range(8);
    TEST_ASSERT(base2 != IRQ_INVALID, "Second range allocated");
    
    // Should not overlap with remaining allocated parts
    TEST_ASSERT(base2 < base1 || base2 >= base1 + 10, 
               "No overlap with partial free");
    TEST_PASS("Overlapping range handling");
    
    // Clean up
    virq_free_range(base1, 3);
    virq_free_range(base1 + 7, 3);
    if (base2 != IRQ_INVALID) {
        virq_free_range(base2, 8);
    }
}

// Edge case: Domain edge conditions
static void test_domain_edges(void) {
    struct irq_domain *domain;
    uint32_t virq;
    
    // Create domain with size 1
    domain = irq_domain_create_linear(NULL, 1, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Size 1 domain created");
    
    virq = irq_create_mapping(domain, 0);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping at index 0");
    
    virq = irq_create_mapping(domain, 1);
    TEST_ASSERT(virq == IRQ_INVALID, "Mapping at size rejected");
    TEST_PASS("Size 1 domain");
    
    irq_domain_remove(domain);
    
    // Create maximum reasonable size domain
    domain = irq_domain_create_linear(NULL, 1024, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Large domain created");
    
    virq = irq_create_mapping(domain, 1023);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping at max index");
    
    virq = irq_create_mapping(domain, 1024);
    TEST_ASSERT(virq == IRQ_INVALID, "Mapping beyond max rejected");
    TEST_PASS("Large domain boundaries");
    
    irq_domain_remove(domain);
}

// Edge case: Descriptor state transitions
static void test_state_transitions(void) {
    uint32_t virq;
    struct irq_desc *desc;
    int data = 0x1234;
    
    // Setup
    virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    
    // Test enable without handler
    enable_irq(virq);  // Should handle gracefully
    TEST_ASSERT(desc->depth == 0, "Enable without handler");
    
    // Multiple enables
    enable_irq(virq);
    enable_irq(virq);
    TEST_ASSERT(desc->depth == 0, "Multiple enables clamped");
    TEST_PASS("Enable state transitions");
    
    // Register handler
    request_irq(virq, edge_handler, 0, "edge", &data);
    TEST_ASSERT(desc->depth == 0, "Depth after request");
    
    // Deeply nested disable
    for (int i = 0; i < 10; i++) {
        disable_irq_nosync(virq);
    }
    TEST_ASSERT(desc->depth == 10, "Deep nesting");
    
    // Unwind all
    for (int i = 0; i < 10; i++) {
        enable_irq(virq);
    }
    TEST_ASSERT(desc->depth == 0, "Fully unwound");
    TEST_ASSERT(!(desc->status & IRQ_DISABLED), "Enabled after unwind");
    TEST_PASS("Deep nesting handled");
    
    // Clean up
    free_irq(virq, &data);
    irq_desc_free(desc);
    virq_free(virq);
}

// Edge case: Handler chain corruption protection
static void test_handler_chain_integrity(void) {
    uint32_t virq;
    struct irq_desc *desc;
    int data[5] = {1, 2, 3, 4, 5};
    int i;
    
    // Setup
    virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    
    // Add shared handlers
    for (i = 0; i < 5; i++) {
        request_irq(virq, edge_handler, IRQF_SHARED, "shared", &data[i]);
    }
    
    // Free same data twice (should handle gracefully)
    free_irq(virq, &data[2]);
    free_irq(virq, &data[2]);  // Duplicate free
    TEST_PASS("Duplicate free handled");
    
    // Free with wrong data (should not remove anything)
    int wrong_data = 999;
    free_irq(virq, &wrong_data);
    
    // Count remaining handlers
    struct irq_action *action = desc->action;
    int count = 0;
    while (action) {
        count++;
        action = action->next;
    }
    TEST_ASSERT(count == 4, "Chain integrity maintained");
    TEST_PASS("Wrong data free ignored");
    
    // Clean up remaining
    for (i = 0; i < 5; i++) {
        if (i != 2) {  // Already freed
            free_irq(virq, &data[i]);
        }
    }
    
    irq_desc_free(desc);
    virq_free(virq);
}

// Edge case: Resource exhaustion
static void test_resource_exhaustion(void) {
    struct irq_domain *domains[100];
    uint32_t virqs[100];
    int max_domains = 0;
    int max_virqs = 0;
    int i;
    
    // Try to exhaust domain creation
    for (i = 0; i < 100; i++) {
        domains[i] = irq_domain_create_linear(NULL, 32, NULL, NULL);
        if (!domains[i]) {
            break;
        }
        max_domains++;
    }
    
    uart_puts("  Created ");
    uart_putdec(max_domains);
    uart_puts(" domains before exhaustion\n");
    TEST_ASSERT(max_domains > 10, "Reasonable number of domains");
    TEST_PASS("Domain exhaustion handled");
    
    // Clean up domains
    for (i = 0; i < max_domains; i++) {
        irq_domain_remove(domains[i]);
    }
    
    // Try to exhaust virq allocation
    for (i = 0; i < 100; i++) {
        virqs[i] = virq_alloc();
        if (virqs[i] == IRQ_INVALID) {
            break;
        }
        max_virqs++;
    }
    
    uart_puts("  Allocated ");
    uart_putdec(max_virqs);
    uart_puts(" virqs before exhaustion\n");
    TEST_ASSERT(max_virqs > 50, "Reasonable number of virqs");
    TEST_PASS("Virq exhaustion handled");
    
    // Clean up virqs
    for (i = 0; i < max_virqs; i++) {
        virq_free(virqs[i]);
    }
}

// Edge case: Concurrent-like operations (sequential but testing race-like conditions)
static void test_concurrent_scenarios(void) {
    struct irq_domain *domain1, *domain2;
    uint32_t virq1, virq2;
    int data = 0x1234;
    
    // Create two domains
    domain1 = irq_domain_create_linear(NULL, 32, NULL, NULL);
    domain2 = irq_domain_create_linear(NULL, 32, NULL, NULL);
    
    // Same hwirq in different domains
    virq1 = irq_create_mapping(domain1, 10);
    virq2 = irq_create_mapping(domain2, 10);
    TEST_ASSERT(virq1 != virq2, "Different virqs for same hwirq");
    TEST_PASS("Same hwirq different domains");
    
    // Register handlers on both
    request_irq(virq1, edge_handler, 0, "dom1", &data);
    request_irq(virq2, edge_handler, 0, "dom2", &data);
    
    // Trigger both
    edge_handler_called = 0;
    generic_handle_irq(virq1);
    TEST_ASSERT(edge_handler_called == 1, "First handler called");
    
    generic_handle_irq(virq2);
    TEST_ASSERT(edge_handler_called == 2, "Second handler called");
    TEST_PASS("Independent domain handling");
    
    // Remove domain1 while virq2 still active
    free_irq(virq1, &data);
    irq_dispose_mapping(virq1);
    irq_domain_remove(domain1);
    
    // virq2 should still work
    edge_handler_called = 0;
    generic_handle_irq(virq2);
    TEST_ASSERT(edge_handler_called == 1, "virq2 still works");
    TEST_PASS("Domain isolation");
    
    // Clean up
    free_irq(virq2, &data);
    irq_dispose_mapping(virq2);
    irq_domain_remove(domain2);
}

// Edge case: Invalid domain operations
static void test_invalid_domain_ops(void) {
    struct irq_domain *domain;
    uint32_t virq;
    int ret;
    
    // Operations on NULL domain
    virq = irq_create_mapping(NULL, 10);
    // Should use default domain or fail gracefully
    if (virq != IRQ_INVALID) {
        irq_dispose_mapping(virq);
    }
    TEST_PASS("NULL domain handled");
    
    // Create domain
    domain = irq_domain_create_linear(NULL, 16, NULL, NULL);
    
    // Double dispose
    virq = irq_create_mapping(domain, 5);
    irq_dispose_mapping(virq);
    irq_dispose_mapping(virq);  // Second dispose
    TEST_PASS("Double dispose handled");
    
    // Bulk operations with invalid parameters
    ret = irq_domain_alloc_irqs(NULL, 4, NULL, NULL);
    TEST_ASSERT(ret == -1, "NULL domain bulk alloc rejected");
    
    ret = irq_domain_alloc_irqs(domain, -5, NULL, NULL);
    TEST_ASSERT(ret == -1, "Negative count rejected");
    
    irq_domain_free_irqs(IRQ_INVALID, 4);  // Should handle gracefully
    TEST_PASS("Invalid bulk operations handled");
    
    irq_domain_remove(domain);
}

// Main edge case test runner
void run_irq_edge_tests(void) {
    uart_puts("\n========================================\n");
    uart_puts("   IRQ EDGE CASE & ERROR TESTS\n");
    uart_puts("========================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Initialize IRQ subsystem
    irq_init();
    
    // Run edge case tests
    test_boundary_values();
    test_overlapping_operations();
    test_domain_edges();
    test_state_transitions();
    test_handler_chain_integrity();
    test_resource_exhaustion();
    test_concurrent_scenarios();
    test_invalid_domain_ops();
    
    // Summary
    uart_puts("\nEdge Case Test Summary:\n");
    uart_puts("  PASSED: ");
    uart_putdec(tests_passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
}