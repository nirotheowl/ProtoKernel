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

// Mock interrupt controller chip
static int mock_handler_called = 0;
static void *mock_handler_data = NULL;

static void mock_irq_mask(struct irq_desc *desc) {
    desc->status |= IRQ_MASKED;
}

static void mock_irq_unmask(struct irq_desc *desc) {
    desc->status &= ~IRQ_MASKED;
}

static void mock_irq_ack(struct irq_desc *desc) {
    // Acknowledge interrupt
}

static void mock_irq_eoi(struct irq_desc *desc) {
    // End of interrupt
}

static struct irq_chip mock_irq_chip = {
    .name = "mock",
    .irq_mask = mock_irq_mask,
    .irq_unmask = mock_irq_unmask,
    .irq_ack = mock_irq_ack,
    .irq_eoi = mock_irq_eoi,
};

// Mock interrupt handler
static void mock_interrupt_handler(void *data) {
    mock_handler_called++;
    mock_handler_data = data;
}

// Test virtual IRQ allocator
static void test_virq_allocator(void) {
    uint32_t virq1, virq2, virq_range;
    
    uart_puts("\n=== Testing Virtual IRQ Allocator ===\n");
    
    // Initialize allocator
    virq_allocator_init();
    
    // Test single allocation
    virq1 = virq_alloc();
    TEST_ASSERT(virq1 != IRQ_INVALID, "Single virq allocation");
    TEST_ASSERT(virq1 > 0, "virq should be > 0 (0 is reserved)");
    TEST_PASS("Single virq allocation");
    
    // Test second allocation
    virq2 = virq_alloc();
    TEST_ASSERT(virq2 != IRQ_INVALID, "Second virq allocation");
    TEST_ASSERT(virq2 != virq1, "virqs should be unique");
    TEST_PASS("Second virq allocation");
    
    // Test range allocation
    virq_range = virq_alloc_range(4);
    TEST_ASSERT(virq_range != IRQ_INVALID, "Range allocation");
    TEST_PASS("Range allocation of 4 virqs");
    
    // Test is_allocated
    TEST_ASSERT(virq_is_allocated(virq1), "virq1 should be allocated");
    TEST_ASSERT(virq_is_allocated(virq_range), "Range start should be allocated");
    TEST_ASSERT(virq_is_allocated(virq_range + 3), "Range end should be allocated");
    TEST_PASS("Allocation status check");
    
    // Test free
    virq_free(virq1);
    TEST_ASSERT(!virq_is_allocated(virq1), "virq1 should be freed");
    TEST_PASS("Single virq free");
    
    // Test range free
    virq_free_range(virq_range, 4);
    TEST_ASSERT(!virq_is_allocated(virq_range), "Range should be freed");
    TEST_ASSERT(!virq_is_allocated(virq_range + 3), "Range end should be freed");
    TEST_PASS("Range free");
    
    // Test reallocation of freed virq
    uint32_t virq3 = virq_alloc();
    TEST_ASSERT(virq3 == virq1, "Should reuse freed virq");
    TEST_PASS("Reallocation of freed virq");
}

// Test IRQ descriptor management
static void test_irq_descriptors(void) {
    struct irq_desc *desc1, *desc2;
    uint32_t virq;
    
    uart_puts("\n=== Testing IRQ Descriptors ===\n");
    
    // Initialize IRQ subsystem
    irq_init();
    
    // Allocate a virq
    virq = virq_alloc();
    TEST_ASSERT(virq != IRQ_INVALID, "virq allocation for descriptor test");
    
    // Allocate descriptor
    desc1 = irq_desc_alloc(virq);
    TEST_ASSERT(desc1 != NULL, "Descriptor allocation");
    TEST_ASSERT(desc1->irq == virq, "Descriptor virq field");
    TEST_ASSERT(desc1->hwirq == IRQ_INVALID, "Initial hwirq should be invalid");
    TEST_ASSERT(desc1->status & IRQ_DISABLED, "Initial status should be disabled");
    TEST_PASS("Descriptor allocation");
    
    // Test irq_to_desc
    desc2 = irq_to_desc(virq);
    TEST_ASSERT(desc2 == desc1, "irq_to_desc should return same descriptor");
    TEST_PASS("irq_to_desc lookup");
    
    // Test double allocation (should return same descriptor)
    desc2 = irq_desc_alloc(virq);
    TEST_ASSERT(desc2 == desc1, "Double allocation should return same descriptor");
    TEST_PASS("Descriptor double allocation protection");
    
    // Free descriptor
    irq_desc_free(desc1);
    desc2 = irq_to_desc(virq);
    TEST_ASSERT(desc2 == NULL, "Descriptor should be freed");
    TEST_PASS("Descriptor free");
}

// Test linear domain creation and mapping
static void test_linear_domain(void) {
    struct irq_domain *domain;
    uint32_t virq1, virq2;
    
    uart_puts("\n=== Testing Linear Domain ===\n");
    
    // Create linear domain
    domain = irq_domain_create_linear(NULL, 32, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Linear domain creation");
    TEST_ASSERT(domain->type == DOMAIN_LINEAR, "Domain type");
    TEST_ASSERT(domain->size == 32, "Domain size");
    TEST_PASS("Linear domain creation");
    
    // Set chip for domain
    domain->chip = &mock_irq_chip;
    
    // Create mapping
    virq1 = irq_create_mapping(domain, 5);  // Map hwirq 5
    TEST_ASSERT(virq1 != IRQ_INVALID, "Create mapping for hwirq 5");
    TEST_PASS("Create mapping");
    
    // Find existing mapping
    virq2 = irq_find_mapping(domain, 5);
    TEST_ASSERT(virq2 == virq1, "Find mapping should return same virq");
    TEST_PASS("Find existing mapping");
    
    // Create another mapping
    virq2 = irq_create_mapping(domain, 10);  // Map hwirq 10
    TEST_ASSERT(virq2 != IRQ_INVALID, "Create mapping for hwirq 10");
    TEST_ASSERT(virq2 != virq1, "Different hwirqs should get different virqs");
    TEST_PASS("Create second mapping");
    
    // Test descriptor setup
    struct irq_desc *desc = irq_to_desc(virq1);
    TEST_ASSERT(desc != NULL, "Descriptor should exist");
    TEST_ASSERT(desc->hwirq == 5, "Hardware IRQ should be set");
    TEST_ASSERT(desc->domain == domain, "Domain should be set");
    TEST_ASSERT(desc->chip == &mock_irq_chip, "Chip should be set");
    TEST_PASS("Descriptor setup through mapping");
    
    // Test out of range hwirq
    uint32_t bad_virq = irq_create_mapping(domain, 100);  // Out of range
    TEST_ASSERT(bad_virq == IRQ_INVALID, "Out of range hwirq should fail");
    TEST_PASS("Out of range protection");
    
    // Dispose mapping
    irq_dispose_mapping(virq1);
    virq2 = irq_find_mapping(domain, 5);
    TEST_ASSERT(virq2 == IRQ_INVALID, "Disposed mapping should not be found");
    TEST_PASS("Dispose mapping");
    
    // Clean up
    irq_domain_remove(domain);
}

// Test interrupt request and handling
static void test_interrupt_handling(void) {
    struct irq_domain *domain;
    uint32_t virq;
    int ret;
    int test_data = 0x1234;
    
    uart_puts("\n=== Testing Interrupt Handling ===\n");
    
    // Setup
    mock_handler_called = 0;
    mock_handler_data = NULL;
    
    // Create domain and mapping
    domain = irq_domain_create_linear(NULL, 16, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Domain creation for handler test");
    domain->chip = &mock_irq_chip;
    
    virq = irq_create_mapping(domain, 3);
    TEST_ASSERT(virq != IRQ_INVALID, "Create mapping for handler test");
    
    // Request IRQ
    ret = request_irq(virq, mock_interrupt_handler, 0, "test", &test_data);
    TEST_ASSERT(ret == 0, "Request IRQ");
    TEST_PASS("Request IRQ");
    
    // Check descriptor state
    struct irq_desc *desc = irq_to_desc(virq);
    TEST_ASSERT(desc->action != NULL, "Action should be installed");
    TEST_ASSERT(!(desc->status & IRQ_DISABLED), "IRQ should be enabled after request");
    TEST_PASS("IRQ enabled after request");
    
    // Simulate interrupt
    generic_handle_irq(virq);
    TEST_ASSERT(mock_handler_called == 1, "Handler should be called");
    TEST_ASSERT(mock_handler_data == &test_data, "Handler data should match");
    TEST_PASS("Interrupt handler called");
    
    // Test disable/enable
    disable_irq_nosync(virq);
    TEST_ASSERT(desc->status & IRQ_DISABLED, "IRQ should be disabled");
    TEST_PASS("Disable IRQ");
    
    enable_irq(virq);
    TEST_ASSERT(!(desc->status & IRQ_DISABLED), "IRQ should be enabled");
    TEST_PASS("Enable IRQ");
    
    // Free IRQ
    free_irq(virq, &test_data);
    TEST_ASSERT(desc->action == NULL, "Action should be removed");
    TEST_ASSERT(desc->status & IRQ_DISABLED, "IRQ should be disabled after free");
    TEST_PASS("Free IRQ");
    
    // Clean up
    irq_dispose_mapping(virq);
    irq_domain_remove(domain);
}

// Test shared interrupts
static void test_shared_interrupts(void) {
    struct irq_domain *domain;
    uint32_t virq;
    int ret;
    int data1 = 0x1111, data2 = 0x2222;
    
    uart_puts("\n=== Testing Shared Interrupts ===\n");
    
    // Setup
    mock_handler_called = 0;
    
    // Create domain and mapping
    domain = irq_domain_create_linear(NULL, 8, NULL, NULL);
    domain->chip = &mock_irq_chip;
    virq = irq_create_mapping(domain, 1);
    
    // Request first handler with SHARED flag
    ret = request_irq(virq, mock_interrupt_handler, IRQF_SHARED, "test1", &data1);
    TEST_ASSERT(ret == 0, "Request first shared IRQ");
    TEST_PASS("First shared handler");
    
    // Request second handler with SHARED flag
    ret = request_irq(virq, mock_interrupt_handler, IRQF_SHARED, "test2", &data2);
    TEST_ASSERT(ret == 0, "Request second shared IRQ");
    TEST_PASS("Second shared handler");
    
    // Try to request non-shared on shared IRQ (should fail)
    int data3 = 0x3333;
    ret = request_irq(virq, mock_interrupt_handler, 0, "test3", &data3);
    TEST_ASSERT(ret != 0, "Non-shared request on shared IRQ should fail");
    TEST_PASS("Shared/non-shared conflict detection");
    
    // Free handlers
    free_irq(virq, &data1);
    free_irq(virq, &data2);
    
    // Clean up
    irq_dispose_mapping(virq);
    irq_domain_remove(domain);
}

// Test bulk allocation for MSI
static void test_bulk_allocation(void) {
    struct irq_domain *domain;
    int virq_base;
    struct irq_desc *desc;
    
    uart_puts("\n=== Testing Bulk Allocation ===\n");
    
    // Create domain
    domain = irq_domain_create_linear(NULL, 64, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Domain creation for bulk test");
    domain->chip = &mock_irq_chip;
    
    // Allocate 4 consecutive IRQs
    virq_base = irq_domain_alloc_irqs(domain, 4, NULL, NULL);
    TEST_ASSERT(virq_base > 0, "Bulk allocation of 4 IRQs");
    TEST_PASS("Bulk allocation");
    
    // Check all descriptors are allocated
    for (int i = 0; i < 4; i++) {
        desc = irq_to_desc(virq_base + i);
        TEST_ASSERT(desc != NULL, "Descriptor should exist");
        TEST_ASSERT(desc->domain == domain, "Domain should be set");
        TEST_ASSERT(desc->chip == &mock_irq_chip, "Chip should be set");
    }
    TEST_PASS("All bulk descriptors allocated");
    
    // Free bulk allocation
    irq_domain_free_irqs(virq_base, 4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(!virq_is_allocated(virq_base + i), "virq should be freed");
    }
    TEST_PASS("Bulk free");
    
    // Clean up
    irq_domain_remove(domain);
}

// Main test runner
void run_irq_basic_tests(void) {
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("   IRQ SUBSYSTEM BASIC TESTS\n");
    uart_puts("========================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Run all tests
    test_virq_allocator();
    test_irq_descriptors();
    test_linear_domain();
    test_interrupt_handling();
    test_shared_interrupts();
    test_bulk_allocation();
    
    // Summary
    uart_puts("\n========================================\n");
    uart_puts("TEST SUMMARY:\n");
    uart_puts("  PASSED: ");
    uart_putdec(tests_passed);
    uart_puts("\n");
    uart_puts("  FAILED: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    if (tests_failed == 0) {
        uart_puts("  RESULT: ALL TESTS PASSED!\n");
    } else {
        uart_puts("  RESULT: SOME TESTS FAILED\n");
    }
    uart_puts("========================================\n\n");
}