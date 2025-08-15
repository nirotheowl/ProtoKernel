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

// Mock handler for testing
static int handler_call_count = 0;
static void *last_handler_data = NULL;

static void mock_handler(void *data) {
    handler_call_count++;
    last_handler_data = data;
}

static void second_handler(void *data) {
    handler_call_count++;
    last_handler_data = data;
}

// Test descriptor allocation and initialization
static void test_descriptor_allocation(void) {
    struct irq_desc *desc;
    uint32_t virq;
    
    // Allocate a virq first
    virq = virq_alloc();
    TEST_ASSERT(virq != IRQ_INVALID, "virq allocation");
    
    // Allocate descriptor
    desc = irq_desc_alloc(virq);
    TEST_ASSERT(desc != NULL, "Descriptor allocation");
    TEST_ASSERT(desc->irq == virq, "Descriptor virq field");
    TEST_ASSERT(desc->hwirq == IRQ_INVALID, "Initial hwirq");
    TEST_ASSERT(desc->domain == NULL, "Initial domain");
    TEST_ASSERT(desc->chip == NULL, "Initial chip");
    TEST_ASSERT(desc->action == NULL, "Initial action");
    TEST_ASSERT(desc->status & IRQ_DISABLED, "Initially disabled");
    TEST_ASSERT(desc->depth == 1, "Initial depth");
    TEST_ASSERT(desc->count == 0, "Initial count");
    TEST_ASSERT(desc->spurious_count == 0, "Initial spurious count");
    TEST_PASS("Descriptor initialization correct");
    
    // Test double allocation returns same descriptor
    struct irq_desc *desc2 = irq_desc_alloc(virq);
    TEST_ASSERT(desc2 == desc, "Double allocation returns same");
    TEST_PASS("Double allocation protection");
    
    // Free descriptor
    irq_desc_free(desc);
    desc = irq_to_desc(virq);
    TEST_ASSERT(desc == NULL, "Descriptor freed");
    TEST_PASS("Descriptor free");
    
    virq_free(virq);
}

// Test descriptor lookup
static void test_descriptor_lookup(void) {
    struct irq_desc *desc, *lookup;
    uint32_t virqs[5];
    int i;
    
    // Allocate multiple descriptors
    for (i = 0; i < 5; i++) {
        virqs[i] = virq_alloc();
        desc = irq_desc_alloc(virqs[i]);
        TEST_ASSERT(desc != NULL, "Descriptor allocation");
        
        // Set a unique value to verify later
        desc->cpu_mask = i + 1;
    }
    TEST_PASS("Multiple descriptors allocated");
    
    // Look them up and verify
    for (i = 0; i < 5; i++) {
        lookup = irq_to_desc(virqs[i]);
        TEST_ASSERT(lookup != NULL, "Descriptor lookup");
        TEST_ASSERT(lookup->irq == virqs[i], "Correct descriptor");
        TEST_ASSERT(lookup->cpu_mask == i + 1, "Descriptor data preserved");
    }
    TEST_PASS("All descriptors found correctly");
    
    // Test invalid lookup
    lookup = irq_to_desc(IRQ_INVALID);
    TEST_ASSERT(lookup == NULL, "Invalid virq lookup returns NULL");
    
    lookup = irq_to_desc(99999);
    TEST_ASSERT(lookup == NULL, "Out of range lookup returns NULL");
    TEST_PASS("Invalid lookups handled");
    
    // Clean up
    for (i = 0; i < 5; i++) {
        irq_desc_free(irq_to_desc(virqs[i]));
        virq_free(virqs[i]);
    }
}

// Test request_irq and free_irq
static void test_request_free_irq(void) {
    uint32_t virq;
    struct irq_desc *desc;
    int ret;
    int data1 = 0x1234;
    int data2 = 0x5678;
    
    // Setup
    virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    handler_call_count = 0;
    
    // Request IRQ
    ret = request_irq(virq, mock_handler, 0, "test1", &data1);
    TEST_ASSERT(ret == 0, "request_irq should succeed");
    TEST_ASSERT(desc->action != NULL, "Action installed");
    TEST_ASSERT(desc->action->handler == mock_handler, "Handler set");
    TEST_ASSERT(desc->action->dev_data == &data1, "Device data set");
    TEST_ASSERT(desc->action->name != NULL, "Name set");
    TEST_ASSERT(!(desc->status & IRQ_DISABLED), "IRQ enabled");
    TEST_ASSERT(desc->depth == 0, "Depth cleared");
    TEST_PASS("request_irq basic");
    
    // Try to request same IRQ without SHARED flag (should fail)
    ret = request_irq(virq, second_handler, 0, "test2", &data2);
    TEST_ASSERT(ret != 0, "Non-shared request should fail");
    TEST_PASS("Non-shared conflict detected");
    
    // Free IRQ
    free_irq(virq, &data1);
    TEST_ASSERT(desc->action == NULL, "Action removed");
    TEST_ASSERT(desc->status & IRQ_DISABLED, "IRQ disabled");
    TEST_ASSERT(desc->depth == 1, "Depth restored");
    TEST_PASS("free_irq basic");
    
    // Clean up
    irq_desc_free(desc);
    virq_free(virq);
}

// Test shared interrupts
static void test_shared_interrupts(void) {
    uint32_t virq;
    struct irq_desc *desc;
    struct irq_action *action;
    int ret;
    int data[3] = {0x1111, 0x2222, 0x3333};
    int count;
    
    // Setup
    virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    
    // Request first shared handler
    ret = request_irq(virq, mock_handler, IRQF_SHARED, "shared1", &data[0]);
    TEST_ASSERT(ret == 0, "First shared request");
    TEST_ASSERT(desc->action->flags & IRQF_SHARED, "SHARED flag set");
    TEST_PASS("First shared handler");
    
    // Request second shared handler
    ret = request_irq(virq, second_handler, IRQF_SHARED, "shared2", &data[1]);
    TEST_ASSERT(ret == 0, "Second shared request");
    
    // Count handlers in chain
    count = 0;
    for (action = desc->action; action; action = action->next) {
        count++;
    }
    TEST_ASSERT(count == 2, "Two handlers in chain");
    TEST_PASS("Second shared handler");
    
    // Request third shared handler
    ret = request_irq(virq, mock_handler, IRQF_SHARED, "shared3", &data[2]);
    TEST_ASSERT(ret == 0, "Third shared request");
    
    count = 0;
    for (action = desc->action; action; action = action->next) {
        count++;
    }
    TEST_ASSERT(count == 3, "Three handlers in chain");
    TEST_PASS("Third shared handler");
    
    // Try non-shared on shared IRQ (should fail)
    ret = request_irq(virq, mock_handler, 0, "nonshared", NULL);
    TEST_ASSERT(ret != 0, "Non-shared on shared should fail");
    TEST_PASS("Shared/non-shared conflict");
    
    // Free middle handler
    free_irq(virq, &data[1]);
    count = 0;
    for (action = desc->action; action; action = action->next) {
        count++;
    }
    TEST_ASSERT(count == 2, "Two handlers remain");
    TEST_PASS("Middle handler removed");
    
    // Free first handler
    free_irq(virq, &data[0]);
    count = 0;
    for (action = desc->action; action; action = action->next) {
        count++;
    }
    TEST_ASSERT(count == 1, "One handler remains");
    TEST_PASS("First handler removed");
    
    // Free last handler
    free_irq(virq, &data[2]);
    TEST_ASSERT(desc->action == NULL, "All handlers removed");
    TEST_ASSERT(desc->status & IRQ_DISABLED, "IRQ disabled when empty");
    TEST_PASS("All handlers removed");
    
    // Clean up
    irq_desc_free(desc);
    virq_free(virq);
}

// Test interrupt enable/disable
static void test_enable_disable(void) {
    uint32_t virq;
    struct irq_desc *desc;
    int data = 0x1234;
    
    // Setup
    virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    request_irq(virq, mock_handler, 0, "test", &data);
    
    // Initially enabled after request_irq
    TEST_ASSERT(!(desc->status & IRQ_DISABLED), "Initially enabled");
    TEST_ASSERT(desc->depth == 0, "Depth is 0");
    
    // Disable once
    disable_irq_nosync(virq);
    TEST_ASSERT(desc->status & IRQ_DISABLED, "Disabled after disable_irq");
    TEST_ASSERT(desc->depth == 1, "Depth is 1");
    TEST_PASS("Single disable");
    
    // Disable again (nested)
    disable_irq_nosync(virq);
    TEST_ASSERT(desc->status & IRQ_DISABLED, "Still disabled");
    TEST_ASSERT(desc->depth == 2, "Depth is 2");
    TEST_PASS("Nested disable");
    
    // Enable once
    enable_irq(virq);
    TEST_ASSERT(desc->status & IRQ_DISABLED, "Still disabled (nested)");
    TEST_ASSERT(desc->depth == 1, "Depth is 1");
    TEST_PASS("Partial enable");
    
    // Enable again
    enable_irq(virq);
    TEST_ASSERT(!(desc->status & IRQ_DISABLED), "Enabled after balanced enable");
    TEST_ASSERT(desc->depth == 0, "Depth is 0");
    TEST_PASS("Full enable");
    
    // Try to enable when already enabled (should not underflow)
    enable_irq(virq);
    TEST_ASSERT(desc->depth == 0, "Depth stays at 0");
    TEST_PASS("Enable underflow protection");
    
    // Clean up
    free_irq(virq, &data);
    irq_desc_free(desc);
    virq_free(virq);
}

// Test trigger type configuration
static void test_trigger_types(void) {
    uint32_t virq;
    struct irq_desc *desc;
    int data = 0x1234;
    int ret;
    
    // Setup
    virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    
    // Test edge rising
    ret = request_irq(virq, mock_handler, IRQF_TRIGGER_RISING, "rising", &data);
    TEST_ASSERT(ret == 0, "Request with RISING trigger");
    TEST_ASSERT(desc->trigger_type == IRQ_TYPE_EDGE_RISING, "Rising edge set");
    free_irq(virq, &data);
    TEST_PASS("Rising edge trigger");
    
    // Test edge falling
    ret = request_irq(virq, mock_handler, IRQF_TRIGGER_FALLING, "falling", &data);
    TEST_ASSERT(ret == 0, "Request with FALLING trigger");
    TEST_ASSERT(desc->trigger_type == IRQ_TYPE_EDGE_FALLING, "Falling edge set");
    free_irq(virq, &data);
    TEST_PASS("Falling edge trigger");
    
    // Test level high
    ret = request_irq(virq, mock_handler, IRQF_TRIGGER_HIGH, "high", &data);
    TEST_ASSERT(ret == 0, "Request with HIGH trigger");
    TEST_ASSERT(desc->trigger_type == IRQ_TYPE_LEVEL_HIGH, "Level high set");
    free_irq(virq, &data);
    TEST_PASS("Level high trigger");
    
    // Test level low
    ret = request_irq(virq, mock_handler, IRQF_TRIGGER_LOW, "low", &data);
    TEST_ASSERT(ret == 0, "Request with LOW trigger");
    TEST_ASSERT(desc->trigger_type == IRQ_TYPE_LEVEL_LOW, "Level low set");
    free_irq(virq, &data);
    TEST_PASS("Level low trigger");
    
    // Test default (no trigger specified)
    ret = request_irq(virq, mock_handler, 0, "default", &data);
    TEST_ASSERT(ret == 0, "Request with default trigger");
    // Trigger type should not change from previous if not specified
    TEST_ASSERT(desc->trigger_type == IRQ_TYPE_LEVEL_LOW, "Trigger unchanged");
    free_irq(virq, &data);
    TEST_PASS("Default trigger behavior");
    
    // Clean up
    irq_desc_free(desc);
    virq_free(virq);
}

// Test descriptor statistics
static void test_descriptor_stats(void) {
    uint32_t virq;
    struct irq_desc *desc;
    int data = 0x1234;
    
    // Setup
    virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    request_irq(virq, mock_handler, 0, "stats", &data);
    
    // Initial stats
    TEST_ASSERT(desc->count == 0, "Initial count is 0");
    TEST_ASSERT(desc->spurious_count == 0, "Initial spurious is 0");
    TEST_ASSERT(desc->last_timestamp == 0, "Initial timestamp is 0");
    TEST_PASS("Initial statistics");
    
    // Simulate interrupts
    handler_call_count = 0;
    generic_handle_irq(virq);
    TEST_ASSERT(desc->count == 1, "Count incremented");
    TEST_ASSERT(handler_call_count == 1, "Handler called");
    
    generic_handle_irq(virq);
    generic_handle_irq(virq);
    TEST_ASSERT(desc->count == 3, "Count is 3");
    TEST_ASSERT(handler_call_count == 3, "Handler called 3 times");
    TEST_PASS("Interrupt counting");
    
    // Test with disabled IRQ (should not call handler)
    disable_irq_nosync(virq);
    generic_handle_irq(virq);
    TEST_ASSERT(desc->count == 3, "Count unchanged when disabled");
    TEST_ASSERT(handler_call_count == 3, "Handler not called when disabled");
    TEST_PASS("Disabled interrupt handling");
    
    // Re-enable and test
    enable_irq(virq);
    generic_handle_irq(virq);
    TEST_ASSERT(desc->count == 4, "Count incremented after enable");
    TEST_ASSERT(handler_call_count == 4, "Handler called after enable");
    TEST_PASS("Re-enabled interrupt handling");
    
    // Clean up
    free_irq(virq, &data);
    irq_desc_free(desc);
    virq_free(virq);
}

// Test invalid operations
static void test_invalid_operations(void) {
    struct irq_desc *desc;
    int data = 0x1234;
    int ret;
    
    // Test operations on invalid virq
    ret = request_irq(IRQ_INVALID, mock_handler, 0, "invalid", &data);
    TEST_ASSERT(ret != 0, "request_irq with invalid virq fails");
    
    ret = request_irq(99999, mock_handler, 0, "outofrange", &data);
    TEST_ASSERT(ret != 0, "request_irq with out of range virq fails");
    TEST_PASS("Invalid request_irq rejected");
    
    // Test with NULL handler
    uint32_t virq = virq_alloc();
    desc = irq_desc_alloc(virq);
    ret = request_irq(virq, NULL, 0, "null", &data);
    TEST_ASSERT(ret != 0, "request_irq with NULL handler fails");
    TEST_PASS("NULL handler rejected");
    
    // Test free_irq on unregistered IRQ
    free_irq(virq, &data);  // Should not crash
    TEST_PASS("free_irq on unregistered IRQ handled");
    
    // Test operations on freed descriptor
    irq_desc_free(desc);
    desc = irq_to_desc(virq);
    TEST_ASSERT(desc == NULL, "Freed descriptor is NULL");
    
    enable_irq(virq);  // Should not crash
    disable_irq_nosync(virq);  // Should not crash
    free_irq(virq, &data);  // Should not crash
    TEST_PASS("Operations on freed descriptor handled");
    
    virq_free(virq);
}

// Main test runner for descriptors
void run_irq_descriptor_tests(void) {
    uart_puts("\n========================================\n");
    uart_puts("   IRQ DESCRIPTOR COMPREHENSIVE TESTS\n");
    uart_puts("========================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Initialize IRQ subsystem
    irq_init();
    
    // Run all tests
    test_descriptor_allocation();
    test_descriptor_lookup();
    test_request_free_irq();
    test_shared_interrupts();
    test_enable_disable();
    test_trigger_types();
    test_descriptor_stats();
    test_invalid_operations();
    
    // Summary
    uart_puts("\nDescriptor Test Summary:\n");
    uart_puts("  PASSED: ");
    uart_putdec(tests_passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
}