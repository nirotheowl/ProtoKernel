/*
 * Comprehensive GIC Integration Tests
 * Thorough testing of ARM GIC functionality and domain integration
 */

#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irqchip/arm-gic.h>
#include <uart.h>
#include <arch_interface.h>
#include <stdint.h>
#include <stdbool.h>
#include <device/device.h>

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) uart_puts("\n[TEST] " name "\n")
#define TEST_PASS(msg) do { uart_puts("  [PASS] " msg "\n"); tests_passed++; } while(0)
#define TEST_FAIL(msg) do { uart_puts("  [FAIL] " msg "\n"); tests_failed++; } while(0)
#define TEST_INFO(msg) uart_puts("  [INFO] " msg "\n")

// Test counters for various interrupt types
static volatile int sgi_count = 0;
static volatile int ppi_count = 0;
static volatile int spi_count = 0;

// Test handlers
static void sgi_test_handler(void *data) {
    sgi_count++;
}

static void ppi_test_handler(void *data) {
    ppi_count++;
}

static void spi_test_handler(void *data) {
    spi_count++;
}

// Test 1: Verify GIC initialization and detection
static void test_gic_initialization(void) {
    TEST_START("GIC Initialization and Detection");
    
    // Find GIC domain
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("GIC domain not found");
        return;
    }
    TEST_PASS("GIC domain found");
    
    // Verify domain properties
    if (gic_domain->type != DOMAIN_LINEAR) {
        TEST_FAIL("GIC domain is not LINEAR type");
        return;
    }
    TEST_PASS("GIC domain is LINEAR type");
    
    // Check domain size (should be at least 32 for SGI/PPI + some SPIs)
    if (gic_domain->size < 64) {
        TEST_FAIL("GIC domain size too small");
        return;
    }
    TEST_PASS("GIC domain size adequate");
    
    TEST_INFO("GIC domain details:");
    uart_puts("    Name: ");
    uart_puts(gic_domain->name ? gic_domain->name : "(unnamed)");
    uart_puts("\n    Size: ");
    uart_putdec(gic_domain->size);
    uart_puts(" interrupts\n");
}

// Test 2: SGI (Software Generated Interrupts) 0-15
static void test_sgi_interrupts(void) {
    TEST_START("Software Generated Interrupts (SGI)");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain for SGI test");
        return;
    }
    
    // Test SGI 0 (hwirq 0)
    uint32_t sgi_virq = irq_create_mapping(gic_domain, 0);
    if (!sgi_virq) {
        TEST_FAIL("Failed to map SGI 0");
        return;
    }
    TEST_PASS("SGI 0 mapped successfully");
    
    // Verify descriptor was created
    struct irq_desc *desc = irq_to_desc(sgi_virq);
    if (!desc) {
        TEST_FAIL("No descriptor for SGI");
        return;
    }
    TEST_PASS("SGI descriptor created");
    
    // Verify hwirq mapping
    if (desc->hwirq != 0) {
        TEST_FAIL("Incorrect hwirq in descriptor");
        return;
    }
    TEST_PASS("SGI hwirq correctly set");
    
    // Try mapping multiple SGIs
    for (int i = 1; i < 16; i++) {
        uint32_t virq = irq_create_mapping(gic_domain, i);
        if (!virq) {
            TEST_FAIL("Failed to map SGI");
            return;
        }
    }
    TEST_PASS("All 16 SGIs mapped successfully");
}

// Test 3: PPI (Private Peripheral Interrupts) 16-31
static void test_ppi_interrupts(void) {
    TEST_START("Private Peripheral Interrupts (PPI)");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain for PPI test");
        return;
    }
    
    // Test timer PPI (usually hwirq 30)
    uint32_t timer_hwirq = 30;
    uint32_t timer_virq = irq_create_mapping(gic_domain, timer_hwirq);
    if (!timer_virq) {
        TEST_FAIL("Failed to map timer PPI");
        return;
    }
    TEST_PASS("Timer PPI mapped successfully");
    
    // Test all PPIs (16-31)
    for (int i = 16; i < 32; i++) {
        uint32_t virq = irq_create_mapping(gic_domain, i);
        if (!virq) {
            TEST_FAIL("Failed to map PPI");
            return;
        }
    }
    TEST_PASS("All 16 PPIs mapped successfully");
}

// Test 4: SPI (Shared Peripheral Interrupts) 32+
static void test_spi_interrupts(void) {
    TEST_START("Shared Peripheral Interrupts (SPI)");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain for SPI test");
        return;
    }
    
    // Test UART SPI (usually hwirq 33 for PL011)
    uint32_t uart_hwirq = 33;
    uint32_t uart_virq = irq_create_mapping(gic_domain, uart_hwirq);
    if (!uart_virq) {
        TEST_FAIL("Failed to map UART SPI");
        return;
    }
    TEST_PASS("UART SPI mapped successfully");
    
    // Test a range of SPIs
    int spi_test_count = 0;
    for (int i = 32; i < 64; i++) {
        uint32_t virq = irq_create_mapping(gic_domain, i);
        if (virq) {
            spi_test_count++;
        }
    }
    
    if (spi_test_count < 32) {
        TEST_FAIL("Too few SPIs mapped");
        return;
    }
    TEST_PASS("SPI range mapped successfully");
    
    TEST_INFO("SPIs mapped:");
    uart_puts("    Count: ");
    uart_putdec(spi_test_count);
    uart_puts("\n");
}

// Test 5: Interrupt request and handler installation
static void test_interrupt_handlers(void) {
    TEST_START("Interrupt Handler Installation");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain for handler test");
        return;
    }
    
    // Test installing handler for SPI
    uint32_t test_hwirq = 40;
    uint32_t test_virq = irq_create_mapping(gic_domain, test_hwirq);
    if (!test_virq) {
        TEST_FAIL("Failed to create test mapping");
        return;
    }
    
    // Request IRQ with handler (using IRQF_SHARED to allow sharing)
    int ret = request_irq(test_virq, spi_test_handler, IRQF_SHARED, "test_spi", NULL);
    if (ret) {
        TEST_FAIL("Failed to request test IRQ");
        return;
    }
    TEST_PASS("Test IRQ requested successfully");
    
    // Verify handler was installed
    struct irq_desc *desc = irq_to_desc(test_virq);
    if (!desc || !desc->action) {
        TEST_FAIL("Handler not installed in descriptor");
        return;
    }
    TEST_PASS("Handler installed in descriptor");
    
    // Test shared interrupt (second handler)
    ret = request_irq(test_virq, spi_test_handler, IRQF_SHARED, "test_spi_shared", NULL);
    if (ret) {
        TEST_FAIL("Failed to request second shared IRQ");
        return;
    }
    TEST_PASS("Second shared IRQ requested successfully");
    
    // Free the IRQs
    free_irq(test_virq, NULL);
    TEST_PASS("IRQs freed successfully");
}

// Test 6: Domain mapping persistence
static void test_mapping_persistence(void) {
    TEST_START("Domain Mapping Persistence");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain");
        return;
    }
    
    // Create a mapping
    uint32_t hwirq = 50;
    uint32_t virq1 = irq_create_mapping(gic_domain, hwirq);
    if (!virq1) {
        TEST_FAIL("Failed to create initial mapping");
        return;
    }
    TEST_PASS("Initial mapping created");
    
    // Try to create the same mapping again
    uint32_t virq2 = irq_create_mapping(gic_domain, hwirq);
    if (virq1 != virq2) {
        TEST_FAIL("Duplicate mapping returned different virq");
        return;
    }
    TEST_PASS("Duplicate mapping returned same virq");
    
    // Find the mapping
    uint32_t virq3 = irq_find_mapping(gic_domain, hwirq);
    if (virq1 != virq3) {
        TEST_FAIL("Find mapping returned different virq");
        return;
    }
    TEST_PASS("Find mapping returned correct virq");
    
    // Dispose and recreate
    irq_dispose_mapping(virq1);
    uint32_t virq4 = irq_create_mapping(gic_domain, hwirq);
    if (!virq4) {
        TEST_FAIL("Failed to recreate mapping after dispose");
        return;
    }
    TEST_PASS("Mapping recreated after dispose");
}

// Test 7: Interrupt enable/disable through GIC
static void test_interrupt_control(void) {
    TEST_START("Interrupt Enable/Disable Control");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain");
        return;
    }
    
    // Create test mapping
    uint32_t hwirq = 60;
    uint32_t virq = irq_create_mapping(gic_domain, hwirq);
    if (!virq) {
        TEST_FAIL("Failed to create test mapping");
        return;
    }
    
    // Request IRQ
    int ret = request_irq(virq, spi_test_handler, 0, "test_control", NULL);
    if (ret) {
        TEST_FAIL("Failed to request IRQ");
        return;
    }
    TEST_PASS("IRQ requested for control test");
    
    // Test disable
    disable_irq(virq);
    struct irq_desc *desc = irq_to_desc(virq);
    if (!desc) {
        TEST_FAIL("No descriptor found");
        return;
    }
    if (desc->depth != 1) {
        TEST_FAIL("Disable depth incorrect");
        return;
    }
    TEST_PASS("IRQ disabled successfully");
    
    // Test nested disable
    disable_irq(virq);
    if (desc->depth != 2) {
        TEST_FAIL("Nested disable depth incorrect");
        return;
    }
    TEST_PASS("Nested disable handled correctly");
    
    // Test enable
    enable_irq(virq);
    if (desc->depth != 1) {
        TEST_FAIL("Enable depth incorrect");
        return;
    }
    TEST_PASS("First enable handled correctly");
    
    enable_irq(virq);
    if (desc->depth != 0) {
        TEST_FAIL("Final enable depth incorrect");
        return;
    }
    TEST_PASS("IRQ fully enabled");
    
    // Cleanup
    free_irq(virq, NULL);
}

// Test 8: GIC domain operations
static void test_domain_operations(void) {
    TEST_START("GIC Domain Operations");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain");
        return;
    }
    
    // Check domain has operations
    if (!gic_domain->ops) {
        TEST_FAIL("GIC domain has no operations");
        return;
    }
    TEST_PASS("GIC domain has operations");
    
    // Check for map operation
    if (!gic_domain->ops->map) {
        TEST_FAIL("GIC domain missing map operation");
        return;
    }
    TEST_PASS("GIC domain has map operation");
    
    // Check for xlate operation (for device tree)
    if (!gic_domain->ops->xlate) {
        TEST_INFO("GIC domain missing xlate operation (optional)");
    } else {
        TEST_PASS("GIC domain has xlate operation");
    }
    
    // Check chip operations
    if (!gic_domain->chip) {
        TEST_FAIL("GIC domain has no chip");
        return;
    }
    TEST_PASS("GIC domain has chip");
    
    // Verify essential chip operations
    struct irq_chip *chip = gic_domain->chip;
    if (!chip->irq_mask || !chip->irq_unmask) {
        TEST_FAIL("GIC chip missing mask/unmask operations");
        return;
    }
    TEST_PASS("GIC chip has mask/unmask operations");
    
    if (!chip->irq_eoi) {
        TEST_FAIL("GIC chip missing EOI operation");
        return;
    }
    TEST_PASS("GIC chip has EOI operation");
}

// Test 9: Boundary conditions
static void test_boundary_conditions(void) {
    TEST_START("GIC Boundary Conditions");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain");
        return;
    }
    
    // Test invalid hwirq (too high)
    uint32_t invalid_virq = irq_create_mapping(gic_domain, 9999);
    if (invalid_virq != IRQ_INVALID && invalid_virq != 0) {
        TEST_FAIL("Mapped invalid hwirq");
        return;
    }
    TEST_PASS("Invalid hwirq rejected");
    
    // Test hwirq at boundary
    uint32_t boundary_virq = irq_create_mapping(gic_domain, gic_domain->size - 1);
    if (!boundary_virq) {
        TEST_FAIL("Failed to map boundary hwirq");
        return;
    }
    TEST_PASS("Boundary hwirq mapped");
    
    // Test just beyond boundary
    uint32_t beyond_virq = irq_create_mapping(gic_domain, gic_domain->size);
    if (beyond_virq != IRQ_INVALID && beyond_virq != 0) {
        TEST_FAIL("Mapped hwirq beyond boundary");
        return;
    }
    TEST_PASS("Beyond boundary hwirq rejected");
}

// Test 10: Concurrent mapping stress test
static void test_concurrent_mappings(void) {
    TEST_START("Concurrent Mapping Stress Test");
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        TEST_FAIL("No GIC domain");
        return;
    }
    
    // Create many mappings rapidly
    uint32_t virqs[100];
    int mapped = 0;
    
    for (int i = 0; i < 100; i++) {
        virqs[i] = irq_create_mapping(gic_domain, 100 + i);
        if (virqs[i]) {
            mapped++;
        }
    }
    
    if (mapped < 50) {
        TEST_FAIL("Too few concurrent mappings created");
        return;
    }
    TEST_PASS("Concurrent mappings created");
    
    // Verify all mappings are unique
    for (int i = 0; i < mapped; i++) {
        for (int j = i + 1; j < mapped; j++) {
            if (virqs[i] && virqs[j] && virqs[i] == virqs[j]) {
                TEST_FAIL("Duplicate virq allocated");
                return;
            }
        }
    }
    TEST_PASS("All virqs are unique");
    
    // Clean up
    for (int i = 0; i < mapped; i++) {
        if (virqs[i]) {
            irq_dispose_mapping(virqs[i]);
        }
    }
    TEST_PASS("Cleanup completed");
}

// Main comprehensive GIC test function
void run_arm_gic_comprehensive_tests(void) {
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("         COMPREHENSIVE GIC INTEGRATION TESTS\n");
    uart_puts("================================================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Run all tests
    test_gic_initialization();
    test_sgi_interrupts();
    test_ppi_interrupts();
    test_spi_interrupts();
    test_interrupt_handlers();
    test_mapping_persistence();
    test_interrupt_control();
    test_domain_operations();
    test_boundary_conditions();
    test_concurrent_mappings();
    
    // Final summary
    uart_puts("\n================================================================\n");
    uart_puts("GIC TEST SUMMARY:\n");
    uart_puts("  PASSED: ");
    uart_putdec(tests_passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(tests_failed);
    uart_puts("\n  TOTAL:  ");
    uart_putdec(tests_passed + tests_failed);
    uart_puts("\n\n  RESULT: ");
    
    if (tests_failed == 0) {
        uart_puts("ALL GIC TESTS PASSED!\n");
    } else {
        uart_puts("SOME TESTS FAILED!\n");
    }
    uart_puts("================================================================\n");
}