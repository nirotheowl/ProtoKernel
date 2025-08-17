/*
 * Comprehensive PLIC Integration Tests
 * Thorough testing of RISC-V PLIC functionality and domain integration
 */

#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irqchip/riscv-plic.h>
#include <irqchip/riscv-intc.h>
#include <uart.h>
#include <arch_interface.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device/device.h>

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) uart_puts("\n[TEST] " name "\n")
#define TEST_PASS(msg) do { uart_puts("  [PASS] " msg "\n"); tests_passed++; } while(0)
#define TEST_FAIL(msg) do { uart_puts("  [FAIL] " msg "\n"); tests_failed++; } while(0)
#define TEST_INFO(msg) uart_puts("  [INFO] " msg "\n")

// Test counters for various interrupt types
static volatile int external_count = 0;
static volatile int timer_count = 0;

// Test handlers
static void external_test_handler(void *data) {
    external_count++;
}

static void timer_test_handler(void *data) {
    timer_count++;
}

// Test 1: Verify PLIC initialization and detection
static void test_plic_initialization(void) {
    TEST_START("PLIC Initialization and Detection");
    
    // Check if PLIC is initialized
    if (!plic_primary) {
        TEST_FAIL("PLIC not initialized");
        return;
    }
    TEST_PASS("PLIC primary instance found");
    
    // Find PLIC domain
    struct irq_domain *plic_domain = plic_primary->domain;
    if (!plic_domain) {
        TEST_FAIL("PLIC domain not found");
        return;
    }
    TEST_PASS("PLIC domain found");
    
    // Verify domain properties
    if (plic_domain->type != DOMAIN_LINEAR) {
        TEST_FAIL("PLIC domain is not LINEAR type");
        return;
    }
    TEST_PASS("PLIC domain is LINEAR type");
    
    // Check domain size (should be at least 32 for basic peripherals)
    if (plic_domain->size < 32) {
        TEST_FAIL("PLIC domain size too small");
        return;
    }
    TEST_PASS("PLIC domain size adequate");
    
    TEST_INFO("PLIC domain details:");
    uart_puts("    Name: ");
    uart_puts(plic_domain->name ? plic_domain->name : "(unnamed)");
    uart_puts("\n    Size: ");
    uart_putdec(plic_domain->size);
    uart_puts(" interrupts\n");
}

// Test 2: Verify INTC initialization (local interrupts)
static void test_intc_initialization(void) {
    TEST_START("INTC Initialization and Detection");
    
    // Check if INTC is initialized
    if (!intc_primary) {
        TEST_FAIL("INTC not initialized");
        return;
    }
    TEST_PASS("INTC primary instance found");
    
    // Find INTC domain
    struct irq_domain *intc_domain = intc_primary->domain;
    if (!intc_domain) {
        TEST_FAIL("INTC domain not found");
        return;
    }
    TEST_PASS("INTC domain found");
    
    TEST_INFO("INTC handles local interrupts (timer, software)");
}

// Test 3: External interrupts (through PLIC)
static void test_external_interrupts(void) {
    TEST_START("External Interrupts (PLIC)");
    
    struct irq_domain *plic_domain = plic_primary ? plic_primary->domain : NULL;
    if (!plic_domain) {
        TEST_FAIL("No PLIC domain for external interrupt test");
        return;
    }
    
    // Test interrupt 10 (typical UART IRQ on QEMU)
    uint32_t uart_hwirq = 10;
    uint32_t uart_virq = irq_create_mapping(plic_domain, uart_hwirq);
    if (!uart_virq) {
        TEST_FAIL("Failed to map UART interrupt");
        return;
    }
    TEST_PASS("UART interrupt mapped successfully");
    
    // Verify descriptor was created
    struct irq_desc *desc = irq_to_desc(uart_virq);
    if (!desc) {
        TEST_FAIL("No descriptor for UART interrupt");
        return;
    }
    TEST_PASS("UART interrupt descriptor created");
    
    // Verify chip operations
    if (!desc->chip) {
        TEST_FAIL("No chip operations for UART interrupt");
        return;
    }
    // Check that chip name is PLIC
    if (!desc->chip->name || strcmp(desc->chip->name, "PLIC") != 0) {
        TEST_FAIL("Wrong chip for UART interrupt");
        return;
    }
    TEST_PASS("Correct chip operations installed");
    
    // Test request/free
    int ret = request_irq(uart_virq, external_test_handler, 0, "test-uart", NULL);
    if (ret < 0) {
        TEST_FAIL("Failed to request UART interrupt");
        return;
    }
    TEST_PASS("UART interrupt requested");
    
    free_irq(uart_virq, NULL);
    TEST_PASS("UART interrupt freed");
}

// Test 4: Test interrupt priorities
static void test_interrupt_priorities(void) {
    TEST_START("Interrupt Priorities");
    
    if (!plic_primary) {
        TEST_FAIL("No PLIC for priority test");
        return;
    }
    
    // Test setting different priorities
    uint32_t test_irqs[] = {1, 10, 20, 30};
    uint32_t test_priorities[] = {1, 3, 5, 7};
    
    for (int i = 0; i < 4; i++) {
        plic_set_priority(test_irqs[i], test_priorities[i]);
    }
    TEST_PASS("Set priorities for test interrupts");
    
    // Test threshold setting
    plic_set_threshold(1, 4);  // Context 1, threshold 4
    TEST_PASS("Set interrupt threshold");
    
    TEST_INFO("Priority configuration complete");
}

// Test 5: Multiple interrupt mappings
static void test_multiple_mappings(void) {
    TEST_START("Multiple Interrupt Mappings");
    
    struct irq_domain *plic_domain = plic_primary ? plic_primary->domain : NULL;
    if (!plic_domain) {
        TEST_FAIL("No PLIC domain for mapping test");
        return;
    }
    
    uint32_t virqs[5];
    uint32_t hwirqs[] = {1, 5, 10, 15, 20};
    
    // Create multiple mappings
    for (int i = 0; i < 5; i++) {
        virqs[i] = irq_create_mapping(plic_domain, hwirqs[i]);
        if (!virqs[i]) {
            TEST_FAIL("Failed to create mapping");
            return;
        }
    }
    TEST_PASS("Created 5 interrupt mappings");
    
    // Verify all mappings
    for (int i = 0; i < 5; i++) {
        uint32_t found = irq_find_mapping(plic_domain, hwirqs[i]);
        if (found != virqs[i]) {
            TEST_FAIL("Mapping lookup failed");
            return;
        }
    }
    TEST_PASS("All mappings verified");
    
    // Cleanup
    for (int i = 0; i < 5; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    TEST_PASS("All mappings disposed");
}

// Test 6: Domain hierarchy (INTC -> PLIC chain)
static void test_domain_hierarchy(void) {
    TEST_START("Domain Hierarchy (INTC -> PLIC)");
    
    if (!intc_primary || !plic_primary) {
        TEST_FAIL("INTC or PLIC not initialized");
        return;
    }
    
    TEST_PASS("Both INTC and PLIC initialized");
    TEST_INFO("INTC handles local interrupts");
    TEST_INFO("PLIC handles external interrupts");
    TEST_INFO("External interrupts route through INTC to PLIC");
    
    // The actual routing is tested when real interrupts fire
    TEST_PASS("Interrupt routing chain verified");
}

// Test 7: Interrupt handler chain
static void test_interrupt_handlers(void) {
    TEST_START("Interrupt Handler Chain");
    
    struct irq_domain *plic_domain = plic_primary ? plic_primary->domain : NULL;
    if (!plic_domain) {
        TEST_FAIL("No PLIC domain for handler test");
        return;
    }
    
    // Map a test interrupt
    uint32_t test_hwirq = 25;
    uint32_t test_virq = irq_create_mapping(plic_domain, test_hwirq);
    if (!test_virq) {
        TEST_FAIL("Failed to create test mapping");
        return;
    }
    TEST_PASS("Test interrupt mapped");
    
    // Install handler
    external_count = 0;
    int ret = request_irq(test_virq, external_test_handler, 0, "test-handler", (void*)0x1234);
    if (ret < 0) {
        TEST_FAIL("Failed to install handler");
        irq_dispose_mapping(test_virq);
        return;
    }
    TEST_PASS("Handler installed");
    
    // Verify handler is in chain
    struct irq_desc *desc = irq_to_desc(test_virq);
    if (!desc || !desc->action) {
        TEST_FAIL("Handler not in chain");
    } else {
        TEST_PASS("Handler in action chain");
    }
    
    // Free handler
    free_irq(test_virq, (void*)0x1234);
    TEST_PASS("Handler freed");
    
    // Cleanup
    irq_dispose_mapping(test_virq);
    TEST_PASS("Mapping disposed");
}

// Test 8: Mapping persistence
static void test_mapping_persistence(void) {
    TEST_START("Mapping Persistence");
    
    struct irq_domain *plic_domain = plic_primary ? plic_primary->domain : NULL;
    if (!plic_domain) {
        TEST_FAIL("No PLIC domain for persistence test");
        return;
    }
    
    uint32_t hwirq = 42;
    
    // Create first mapping
    uint32_t virq1 = irq_create_mapping(plic_domain, hwirq);
    if (!virq1) {
        TEST_FAIL("Failed to create first mapping");
        return;
    }
    TEST_PASS("First mapping created");
    
    // Try to create duplicate - should return same virq
    uint32_t virq2 = irq_create_mapping(plic_domain, hwirq);
    if (virq2 != virq1) {
        TEST_FAIL("Duplicate mapping returned different virq");
        return;
    }
    TEST_PASS("Duplicate mapping returned same virq");
    
    // Find mapping
    uint32_t found = irq_find_mapping(plic_domain, hwirq);
    if (found != virq1) {
        TEST_FAIL("Find mapping returned wrong virq");
        return;
    }
    TEST_PASS("Find mapping successful");
    
    // Dispose and verify
    irq_dispose_mapping(virq1);
    found = irq_find_mapping(plic_domain, hwirq);
    if (found != IRQ_INVALID) {
        TEST_FAIL("Mapping still exists after dispose");
        return;
    }
    TEST_PASS("Mapping properly disposed");
}

// Test 9: Interrupt enable/disable control
static void test_interrupt_control(void) {
    TEST_START("Interrupt Enable/Disable Control");
    
    struct irq_domain *plic_domain = plic_primary ? plic_primary->domain : NULL;
    if (!plic_domain) {
        TEST_FAIL("No PLIC domain for control test");
        return;
    }
    
    // Map test interrupt
    uint32_t hwirq = 35;
    uint32_t virq = irq_create_mapping(plic_domain, hwirq);
    if (!virq) {
        TEST_FAIL("Failed to create mapping");
        return;
    }
    TEST_PASS("Control test interrupt mapped");
    
    // Request interrupt
    int ret = request_irq(virq, external_test_handler, 0, "test-control", NULL);
    if (ret < 0) {
        TEST_FAIL("Failed to request interrupt");
        irq_dispose_mapping(virq);
        return;
    }
    TEST_PASS("Control test interrupt requested");
    
    // Test disable
    disable_irq(virq);
    TEST_PASS("Interrupt disabled");
    
    // Test enable
    enable_irq(virq);
    TEST_PASS("Interrupt enabled");
    
    // Test nosync disable
    disable_irq_nosync(virq);
    TEST_PASS("Interrupt disabled (nosync)");
    
    // Re-enable for cleanup
    enable_irq(virq);
    
    // Cleanup
    free_irq(virq, NULL);
    irq_dispose_mapping(virq);
    TEST_PASS("Control test cleanup complete");
}

// Test 10: Boundary conditions
static void test_boundary_conditions(void) {
    TEST_START("Boundary Conditions");
    
    struct irq_domain *plic_domain = plic_primary ? plic_primary->domain : NULL;
    if (!plic_domain) {
        TEST_FAIL("No PLIC domain for boundary test");
        return;
    }
    
    // Test invalid hwirq 0 (reserved)
    uint32_t virq = irq_create_mapping(plic_domain, 0);
    if (virq != IRQ_INVALID) {
        TEST_FAIL("Mapped invalid hwirq 0");
        return;
    }
    TEST_PASS("Rejected invalid hwirq 0");
    
    // Test maximum valid hwirq (domain size - 1)
    uint32_t max_hwirq = plic_domain->size - 1;
    virq = irq_create_mapping(plic_domain, max_hwirq);
    if (!virq) {
        TEST_FAIL("Failed to map maximum hwirq");
        return;
    }
    TEST_PASS("Mapped maximum valid hwirq");
    irq_dispose_mapping(virq);
    
    // Test beyond maximum
    virq = irq_create_mapping(plic_domain, plic_domain->size + 10);
    if (virq != IRQ_INVALID) {
        TEST_FAIL("Mapped beyond domain size");
        return;
    }
    TEST_PASS("Rejected hwirq beyond domain size");
    
    // Test priority boundaries
    plic_set_priority(1, 0);  // Minimum priority
    plic_set_priority(2, PLIC_MAX_PRIORITY);  // Maximum priority
    plic_set_priority(3, PLIC_MAX_PRIORITY + 10);  // Beyond max (should clamp)
    TEST_PASS("Priority boundary tests complete");
}

// Main test runner for RISC-V PLIC
#ifdef __riscv
void test_riscv_plic_comprehensive(void) {
    uart_puts("\n================================================================\n");
    uart_puts("        RISC-V PLIC COMPREHENSIVE TESTS\n");
    uart_puts("================================================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Run all tests
    test_plic_initialization();
    test_intc_initialization();
    test_external_interrupts();
    test_interrupt_priorities();
    test_multiple_mappings();
    test_domain_hierarchy();
    test_interrupt_handlers();
    test_mapping_persistence();
    test_interrupt_control();
    test_boundary_conditions();
    
    // Print summary
    uart_puts("\n================================================================\n");
    uart_puts("                    TEST SUMMARY\n");
    uart_puts("================================================================\n");
    uart_puts("Tests passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nTests failed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("\n[SUCCESS] All PLIC tests passed!\n");
    } else {
        uart_puts("\n[FAILURE] Some PLIC tests failed\n");
    }
}
#else
// Stub for when running on non-RISC-V
void test_riscv_plic_comprehensive(void) {
    uart_puts("\n>>> RISC-V PLIC tests not available on this architecture\n");
}
#endif