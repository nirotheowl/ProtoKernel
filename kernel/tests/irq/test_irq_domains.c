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

// Mock IRQ chip for testing
static void mock_mask(struct irq_desc *desc) {
    desc->status |= IRQ_MASKED;
}

static void mock_unmask(struct irq_desc *desc) {
    desc->status &= ~IRQ_MASKED;
}

static void mock_ack(struct irq_desc *desc) {
    // Acknowledge interrupt
    (void)desc;
}

static struct irq_chip mock_chip = {
    .name = "mock",
    .irq_mask = mock_mask,
    .irq_unmask = mock_unmask,
    .irq_ack = mock_ack,
};

// Mock domain operations
static int mock_map_called = 0;
static int mock_unmap_called = 0;

static int mock_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq) {
    (void)d;
    (void)virq;
    (void)hwirq;
    mock_map_called++;
    return 0;
}

static void mock_unmap(struct irq_domain *d, uint32_t virq) {
    (void)d;
    (void)virq;
    mock_unmap_called++;
}

static struct irq_domain_ops mock_ops = {
    .map = mock_map,
    .unmap = mock_unmap,
};

// Test linear domain creation
static void test_linear_domain_creation(void) {
    struct irq_domain *domain;
    
    // Create linear domain with various sizes
    domain = irq_domain_create_linear(NULL, 32, NULL, NULL);
    TEST_ASSERT(domain != NULL, "32-entry linear domain");
    TEST_ASSERT(domain->type == DOMAIN_LINEAR, "Domain type");
    TEST_ASSERT(domain->size == 32, "Domain size");
    TEST_ASSERT(domain->linear_map != NULL, "Linear map allocated");
    TEST_ASSERT(domain->revmap != NULL, "Reverse map allocated");
    TEST_PASS("Small linear domain");
    irq_domain_remove(domain);
    
    domain = irq_domain_create_linear(NULL, 256, NULL, NULL);
    TEST_ASSERT(domain != NULL, "256-entry linear domain");
    TEST_ASSERT(domain->size == 256, "Large domain size");
    TEST_PASS("Large linear domain");
    irq_domain_remove(domain);
    
    // Test with operations and chip
    domain = irq_domain_create_linear(NULL, 16, &mock_ops, (void *)0x1234);
    TEST_ASSERT(domain != NULL, "Domain with ops");
    TEST_ASSERT(domain->ops == &mock_ops, "Operations set");
    TEST_ASSERT(domain->chip_data == (void *)0x1234, "Chip data set");
    TEST_PASS("Domain with operations");
    irq_domain_remove(domain);
    
    // Test invalid sizes
    domain = irq_domain_create_linear(NULL, 0, NULL, NULL);
    TEST_ASSERT(domain == NULL, "Zero size rejected");
    
    domain = irq_domain_create_linear(NULL, 100000, NULL, NULL);
    TEST_ASSERT(domain == NULL, "Huge size rejected");
    TEST_PASS("Invalid sizes rejected");
}

// Test mapping creation and lookup
static void test_mapping_operations(void) {
    struct irq_domain *domain;
    uint32_t virq1, virq2, virq3;
    struct irq_desc *desc;
    
    // Create domain
    domain = irq_domain_create_linear(NULL, 64, NULL, NULL);
    domain->chip = &mock_chip;
    
    // Create mappings
    virq1 = irq_create_mapping(domain, 10);
    TEST_ASSERT(virq1 != IRQ_INVALID, "First mapping created");
    
    virq2 = irq_create_mapping(domain, 20);
    TEST_ASSERT(virq2 != IRQ_INVALID, "Second mapping created");
    TEST_ASSERT(virq2 != virq1, "Different virqs allocated");
    
    virq3 = irq_create_mapping(domain, 30);
    TEST_ASSERT(virq3 != IRQ_INVALID, "Third mapping created");
    TEST_PASS("Multiple mappings created");
    
    // Verify descriptors are set up correctly
    desc = irq_to_desc(virq1);
    TEST_ASSERT(desc != NULL, "Descriptor exists");
    TEST_ASSERT(desc->hwirq == 10, "hwirq set correctly");
    TEST_ASSERT(desc->domain == domain, "Domain set");
    TEST_ASSERT(desc->chip == &mock_chip, "Chip set");
    TEST_PASS("Descriptor setup correct");
    
    // Test finding existing mappings
    uint32_t found = irq_find_mapping(domain, 10);
    TEST_ASSERT(found == virq1, "Found first mapping");
    
    found = irq_find_mapping(domain, 20);
    TEST_ASSERT(found == virq2, "Found second mapping");
    
    found = irq_find_mapping(domain, 30);
    TEST_ASSERT(found == virq3, "Found third mapping");
    TEST_PASS("Mapping lookup works");
    
    // Test finding non-existent mapping
    found = irq_find_mapping(domain, 40);
    TEST_ASSERT(found == IRQ_INVALID, "Non-existent returns invalid");
    TEST_PASS("Non-existent mapping handled");
    
    // Test duplicate mapping (should return existing)
    uint32_t dup = irq_create_mapping(domain, 10);
    TEST_ASSERT(dup == virq1, "Duplicate returns existing virq");
    TEST_PASS("Duplicate mapping handled");
    
    // Dispose mappings
    irq_dispose_mapping(virq1);
    found = irq_find_mapping(domain, 10);
    TEST_ASSERT(found == IRQ_INVALID, "Disposed mapping not found");
    TEST_PASS("Mapping disposal");
    
    // Clean up
    irq_dispose_mapping(virq2);
    irq_dispose_mapping(virq3);
    irq_domain_remove(domain);
}

// Test domain with operations
static void test_domain_operations(void) {
    struct irq_domain *domain;
    uint32_t virq;
    
    // Reset counters
    mock_map_called = 0;
    mock_unmap_called = 0;
    
    // Create domain with operations
    domain = irq_domain_create_linear(NULL, 32, &mock_ops, NULL);
    
    // Create mapping - should call map operation
    virq = irq_create_mapping(domain, 5);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping created");
    TEST_ASSERT(mock_map_called == 1, "Map operation called");
    TEST_PASS("Map operation invoked");
    
    // Dispose mapping - should call unmap operation
    irq_dispose_mapping(virq);
    TEST_ASSERT(mock_unmap_called == 1, "Unmap operation called");
    TEST_PASS("Unmap operation invoked");
    
    // Create multiple mappings
    mock_map_called = 0;
    uint32_t virqs[3];
    virqs[0] = irq_create_mapping(domain, 1);
    virqs[1] = irq_create_mapping(domain, 2);
    virqs[2] = irq_create_mapping(domain, 3);
    TEST_ASSERT(mock_map_called == 3, "Map called for each");
    TEST_PASS("Multiple map operations");
    
    // Dispose all
    mock_unmap_called = 0;
    for (int i = 0; i < 3; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    TEST_ASSERT(mock_unmap_called == 3, "Unmap called for each");
    TEST_PASS("Multiple unmap operations");
    
    irq_domain_remove(domain);
}

// Test out of range hardware IRQs
static void test_out_of_range(void) {
    struct irq_domain *domain;
    uint32_t virq;
    
    // Create small domain
    domain = irq_domain_create_linear(NULL, 16, NULL, NULL);
    
    // Try to map hwirq beyond domain size
    virq = irq_create_mapping(domain, 16);  // Size is 16, so 0-15 are valid
    TEST_ASSERT(virq == IRQ_INVALID, "Out of range rejected");
    
    virq = irq_create_mapping(domain, 100);
    TEST_ASSERT(virq == IRQ_INVALID, "Far out of range rejected");
    TEST_PASS("Out of range hwirq rejected");
    
    // Valid mappings should work
    virq = irq_create_mapping(domain, 0);
    TEST_ASSERT(virq != IRQ_INVALID, "hwirq 0 valid");
    irq_dispose_mapping(virq);
    
    virq = irq_create_mapping(domain, 15);
    TEST_ASSERT(virq != IRQ_INVALID, "hwirq 15 valid");
    irq_dispose_mapping(virq);
    TEST_PASS("Valid range accepted");
    
    irq_domain_remove(domain);
}

// Test bulk allocation
static void test_bulk_allocation(void) {
    struct irq_domain *domain;
    int virq_base;
    struct irq_desc *desc;
    int i;
    
    // Create domain
    domain = irq_domain_create_linear(NULL, 128, NULL, NULL);
    domain->chip = &mock_chip;
    
    // Allocate 8 consecutive IRQs
    virq_base = irq_domain_alloc_irqs(domain, 8, NULL, NULL);
    TEST_ASSERT(virq_base > 0, "Bulk allocation succeeded");
    TEST_PASS("8 IRQs allocated");
    
    // Verify all descriptors exist and are configured
    for (i = 0; i < 8; i++) {
        desc = irq_to_desc(virq_base + i);
        TEST_ASSERT(desc != NULL, "Descriptor exists");
        TEST_ASSERT(desc->domain == domain, "Domain set");
        TEST_ASSERT(desc->chip == &mock_chip, "Chip set");
        TEST_ASSERT(virq_is_allocated(virq_base + i), "virq allocated");
    }
    TEST_PASS("All bulk descriptors configured");
    
    // Free bulk allocation
    irq_domain_free_irqs(virq_base, 8);
    for (i = 0; i < 8; i++) {
        TEST_ASSERT(!virq_is_allocated(virq_base + i), "virq freed");
    }
    TEST_PASS("Bulk free successful");
    
    // Test invalid bulk allocation
    virq_base = irq_domain_alloc_irqs(NULL, 4, NULL, NULL);
    TEST_ASSERT(virq_base == -1, "NULL domain rejected");
    
    virq_base = irq_domain_alloc_irqs(domain, 0, NULL, NULL);
    TEST_ASSERT(virq_base == -1, "Zero count rejected");
    
    virq_base = irq_domain_alloc_irqs(domain, -5, NULL, NULL);
    TEST_ASSERT(virq_base == -1, "Negative count rejected");
    TEST_PASS("Invalid bulk allocations rejected");
    
    irq_domain_remove(domain);
}

// Test domain removal
static void test_domain_removal(void) {
    struct irq_domain *domain;
    uint32_t virqs[5];
    uint32_t found;
    int i;
    
    // Create domain and mappings
    domain = irq_domain_create_linear(NULL, 32, NULL, NULL);
    for (i = 0; i < 5; i++) {
        virqs[i] = irq_create_mapping(domain, i * 2);
        TEST_ASSERT(virqs[i] != IRQ_INVALID, "Mapping created");
    }
    TEST_PASS("Mappings created for removal test");
    
    // Remove domain - should clean up all mappings
    irq_domain_remove(domain);
    
    // Verify mappings are gone
    for (i = 0; i < 5; i++) {
        // Note: We can't use irq_find_mapping as domain is gone
        // But virqs should be freed
        TEST_ASSERT(!virq_is_allocated(virqs[i]), "virq freed");
    }
    TEST_PASS("Domain removal cleaned up mappings");
}

// Test multiple domains
static void test_multiple_domains(void) {
    struct irq_domain *domain1, *domain2, *domain3;
    uint32_t virq1, virq2, virq3;
    
    // Create multiple domains
    domain1 = irq_domain_create_linear(NULL, 16, NULL, (void *)0x1000);
    domain2 = irq_domain_create_linear(NULL, 32, NULL, (void *)0x2000);
    domain3 = irq_domain_create_linear(NULL, 64, NULL, (void *)0x3000);
    
    TEST_ASSERT(domain1 != NULL, "Domain 1 created");
    TEST_ASSERT(domain2 != NULL, "Domain 2 created");
    TEST_ASSERT(domain3 != NULL, "Domain 3 created");
    TEST_ASSERT(domain1->domain_id != domain2->domain_id, "Unique IDs");
    TEST_ASSERT(domain2->domain_id != domain3->domain_id, "Unique IDs");
    TEST_PASS("Multiple domains created");
    
    // Create mappings in different domains
    virq1 = irq_create_mapping(domain1, 5);
    virq2 = irq_create_mapping(domain2, 5);  // Same hwirq, different domain
    virq3 = irq_create_mapping(domain3, 5);
    
    TEST_ASSERT(virq1 != IRQ_INVALID, "Mapping in domain1");
    TEST_ASSERT(virq2 != IRQ_INVALID, "Mapping in domain2");
    TEST_ASSERT(virq3 != IRQ_INVALID, "Mapping in domain3");
    TEST_ASSERT(virq1 != virq2 && virq2 != virq3, "Different virqs");
    TEST_PASS("Same hwirq in different domains");
    
    // Verify correct domain association
    struct irq_desc *desc1 = irq_to_desc(virq1);
    struct irq_desc *desc2 = irq_to_desc(virq2);
    struct irq_desc *desc3 = irq_to_desc(virq3);
    
    TEST_ASSERT(desc1->domain == domain1, "Correct domain 1");
    TEST_ASSERT(desc2->domain == domain2, "Correct domain 2");
    TEST_ASSERT(desc3->domain == domain3, "Correct domain 3");
    TEST_PASS("Domain associations correct");
    
    // Clean up
    irq_dispose_mapping(virq1);
    irq_dispose_mapping(virq2);
    irq_dispose_mapping(virq3);
    irq_domain_remove(domain1);
    irq_domain_remove(domain2);
    irq_domain_remove(domain3);
}

// Test domain hierarchy functions (basic)
static void test_hierarchy_functions(void) {
    struct irq_domain *domain;
    struct irq_desc *desc;
    uint32_t virq;
    int ret;
    
    // Create domain
    domain = irq_domain_create_linear(NULL, 32, NULL, NULL);
    virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    
    // Test irq_domain_set_hwirq_and_chip
    ret = irq_domain_set_hwirq_and_chip(domain, virq, 10, &mock_chip, (void *)0x5678);
    TEST_ASSERT(ret == 0, "Set hwirq and chip succeeded");
    TEST_ASSERT(desc->hwirq == 10, "hwirq set");
    TEST_ASSERT(desc->chip == &mock_chip, "Chip set");
    TEST_ASSERT(desc->chip_data == (void *)0x5678, "Chip data set");
    TEST_PASS("Set hwirq and chip");
    
    // Test irq_domain_activate_irq
    desc->domain = domain;
    ret = irq_domain_activate_irq(desc, false);
    TEST_ASSERT(ret == 0, "Activate succeeded");
    TEST_PASS("Activate IRQ");
    
    // Test with NULL descriptor
    ret = irq_domain_activate_irq(NULL, false);
    TEST_ASSERT(ret == -1, "NULL descriptor rejected");
    TEST_PASS("NULL descriptor handled");
    
    // Clean up
    irq_desc_free(desc);
    virq_free(virq);
    irq_domain_remove(domain);
}

// Test tree and hierarchy domain stubs
static void test_unimplemented_domains(void) {
    struct irq_domain *domain;
    
    // Test tree domain (should return NULL for now)
    domain = irq_domain_create_tree(NULL, NULL, NULL);
    TEST_ASSERT(domain == NULL, "Tree domain not yet implemented");
    TEST_PASS("Tree domain stub");
    
    // Test hierarchy domain (should return NULL for now)
    domain = irq_domain_create_hierarchy(NULL, 32, NULL, NULL, NULL);
    TEST_ASSERT(domain == NULL, "Hierarchy domain not yet implemented");
    TEST_PASS("Hierarchy domain stub");
}

// Main test runner for domains
void run_irq_domain_tests(void) {
    uart_puts("\n========================================\n");
    uart_puts("   IRQ DOMAIN COMPREHENSIVE TESTS\n");
    uart_puts("========================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Initialize IRQ subsystem
    irq_init();
    
    // Run all tests
    test_linear_domain_creation();
    test_mapping_operations();
    test_domain_operations();
    test_out_of_range();
    test_bulk_allocation();
    test_domain_removal();
    test_multiple_domains();
    test_hierarchy_functions();
    test_unimplemented_domains();
    
    // Summary
    uart_puts("\nDomain Test Summary:\n");
    uart_puts("  PASSED: ");
    uart_putdec(tests_passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
}