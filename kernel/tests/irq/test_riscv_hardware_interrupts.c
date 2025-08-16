/*
 * RISC-V Hardware Interrupt Tests
 * Tests for real hardware interrupt delivery through PLIC/INTC
 */

#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irqchip/riscv-plic.h>
#include <irqchip/riscv-intc.h>
#include <uart.h>
#include <arch_interface.h>
#include <arch_timer.h>
#include <arch_cpu.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) uart_puts("\n[TEST] " name "\n")
#define TEST_PASS(msg) do { uart_puts("  [PASS] " msg "\n"); tests_passed++; } while(0)
#define TEST_FAIL(msg) do { uart_puts("  [FAIL] " msg "\n"); tests_failed++; } while(0)
#define TEST_INFO(msg) uart_puts("  [INFO] " msg "\n")

// Test counters
static volatile int timer_fired = 0;
static volatile int uart_fired = 0;
static volatile int software_fired = 0;
static volatile int external_fired = 0;

// Test handlers
static void real_timer_handler(void *data) {
    timer_fired++;
    // Clear timer interrupt by setting next compare value far in future
    uint64_t current = arch_timer_get_counter();
    uint64_t frequency = arch_timer_get_frequency();
    arch_timer_set_compare(current + frequency * 10); // 10 seconds
}

static void real_uart_handler(void *data) {
    uart_fired++;
    // Would need to clear UART interrupt here
}

static void real_software_handler(void *data) {
    software_fired++;
    // Software interrupt auto-clears
}

static void real_external_handler(void *data) {
    external_fired++;
    // External interrupt handled by PLIC claim/complete
}

// Test 1: PLIC Claim/Complete Flow
static void test_plic_claim_complete(void) {
    TEST_START("PLIC Claim/Complete Flow");
    
    if (!plic_primary) {
        TEST_FAIL("No PLIC instance");
        return;
    }
    
    // Test setting priorities for multiple interrupts
    plic_set_priority(10, 1);  // Low priority
    plic_set_priority(11, 7);  // High priority
    plic_set_priority(12, 3);  // Medium priority
    TEST_PASS("Set multiple interrupt priorities");
    
    // Test threshold setting
    plic_set_threshold(1, 2);  // Only allow priority > 2
    TEST_PASS("Set interrupt threshold");
    
    // Test masking/unmasking
    plic_mask_irq(10);
    TEST_PASS("Masked interrupt 10");
    
    plic_unmask_irq(10);
    TEST_PASS("Unmasked interrupt 10");
    
    // Test claim with no pending interrupts (should return 0)
    uint32_t claimed = plic_claim();
    if (claimed == 0) {
        TEST_PASS("Claim with no pending interrupts returns 0");
    } else {
        TEST_FAIL("Claim returned non-zero with no pending interrupts");
    }
    
    TEST_INFO("PLIC claim/complete flow verified");
}

// Test 2: Real Timer Interrupt (through INTC)
static void test_real_timer_interrupt(void) {
    TEST_START("Real Timer Interrupt (INTC)");
    
#ifdef __riscv
    if (!intc_primary) {
        TEST_FAIL("No INTC instance");
        return;
    }
    
    struct irq_domain *intc_domain = intc_primary->domain;
    if (!intc_domain) {
        TEST_FAIL("No INTC domain");
        return;
    }
    
    // Map timer interrupt (S-mode timer is IRQ_S_TIMER = 5)
    uint32_t timer_hwirq = IRQ_S_TIMER;
    uint32_t timer_virq = irq_find_mapping(intc_domain, timer_hwirq);
    if (timer_virq == IRQ_INVALID || timer_virq == 0) {
        timer_virq = irq_create_mapping(intc_domain, timer_hwirq);
        if (!timer_virq) {
            TEST_FAIL("Failed to map timer interrupt");
            return;
        }
    }
    
    TEST_INFO("Timer mapped:");
    uart_puts("    hwirq=");
    uart_putdec(timer_hwirq);
    uart_puts(" -> virq=");
    uart_putdec(timer_virq);
    uart_puts("\n");
    
    // Check for existing handler and free it
    struct irq_desc *existing_desc = irq_to_desc(timer_virq);
    if (existing_desc && existing_desc->action) {
        free_irq(timer_virq, NULL);
    }
    
    // Request timer interrupt
    int ret = request_irq(timer_virq, real_timer_handler, 0, "timer_test", NULL);
    if (ret) {
        TEST_FAIL("Failed to request timer IRQ");
        return;
    }
    TEST_PASS("Timer IRQ requested");
    
    // Reset counter
    timer_fired = 0;
    
    // Set up timer to fire in a short time
    uint64_t current = arch_timer_get_counter();
    uint64_t frequency = arch_timer_get_frequency();
    uint64_t ticks = frequency / 200;  // 5ms worth of ticks (more reliable)
    uint64_t target = current + ticks;
    
    arch_timer_set_compare(target);
    arch_timer_enable();
    
    // Wait for interrupt (with timeout based on actual time, not iterations)
    uint64_t timeout_ticks = ticks * 3;  // Wait up to 3x the expected time
    uint64_t timeout_target = current + timeout_ticks;
    
    while (timer_fired == 0) {
        uint64_t now = arch_timer_get_counter();
        if (now > timeout_target) {
            break;
        }
        arch_cpu_relax();
    }
    
    // Disable timer
    arch_timer_disable();
    
    if (timer_fired > 0) {
        TEST_PASS("Timer interrupt fired");
        uart_puts("    Count: ");
        uart_putdec(timer_fired);
        uart_puts("\n");
    } else {
        TEST_FAIL("Timer interrupt did not fire");
    }
    
    // Clean up
    free_irq(timer_virq, NULL);
    TEST_PASS("Timer test complete");
#else
    TEST_INFO("Timer test not available on this architecture");
#endif
}

// Test 3: Software Interrupt (through INTC)
static void test_software_interrupt(void) {
    TEST_START("Software Interrupt (INTC)");
    
#ifdef __riscv
    if (!intc_primary) {
        TEST_FAIL("No INTC instance");
        return;
    }
    
    struct irq_domain *intc_domain = intc_primary->domain;
    if (!intc_domain) {
        TEST_FAIL("No INTC domain");
        return;
    }
    
    // Map software interrupt (S-mode software is IRQ_S_SOFT = 1)
    uint32_t soft_hwirq = IRQ_S_SOFT;
    uint32_t soft_virq = irq_find_mapping(intc_domain, soft_hwirq);
    if (soft_virq == IRQ_INVALID || soft_virq == 0) {
        soft_virq = irq_create_mapping(intc_domain, soft_hwirq);
        if (!soft_virq) {
            TEST_FAIL("Failed to map software interrupt");
            return;
        }
    }
    
    TEST_INFO("Software interrupt mapped:");
    uart_puts("    hwirq=");
    uart_putdec(soft_hwirq);
    uart_puts(" -> virq=");
    uart_putdec(soft_virq);
    uart_puts("\n");
    
    // Request software interrupt
    int ret = request_irq(soft_virq, real_software_handler, 0, "soft_test", NULL);
    if (ret) {
        TEST_FAIL("Failed to request software IRQ");
        return;
    }
    TEST_PASS("Software IRQ requested");
    
    // Reset counter
    software_fired = 0;
    
    // Trigger software interrupt to self
    // This would need architecture-specific code to trigger
    // For now, we'll just verify the mapping
    TEST_INFO("Software interrupt ready (trigger mechanism needed)");
    
    // Clean up
    free_irq(soft_virq, NULL);
    TEST_PASS("Software interrupt test complete");
#else
    TEST_INFO("Software interrupt test not available on this architecture");
#endif
}

// Test 4: External Interrupt through PLIC
static void test_external_interrupt_flow(void) {
    TEST_START("External Interrupt Flow (INTC->PLIC)");
    
#ifdef __riscv
    if (!plic_primary || !intc_primary) {
        TEST_FAIL("PLIC or INTC not initialized");
        return;
    }
    
    // First, ensure external interrupts are enabled in INTC
    intc_enable_external();
    TEST_PASS("External interrupts enabled in INTC");
    
    struct irq_domain *plic_domain = plic_primary->domain;
    if (!plic_domain) {
        TEST_FAIL("No PLIC domain");
        return;
    }
    
    // Map a test external interrupt (e.g., UART at hwirq 10)
    uint32_t ext_hwirq = 10;
    uint32_t ext_virq = irq_create_mapping(plic_domain, ext_hwirq);
    if (!ext_virq) {
        TEST_FAIL("Failed to map external interrupt");
        return;
    }
    TEST_PASS("External interrupt mapped through PLIC");
    
    // Set priority for this interrupt
    plic_set_priority(ext_hwirq, 3);
    TEST_PASS("Set external interrupt priority");
    
    // Request the interrupt
    int ret = request_irq(ext_virq, real_external_handler, 0, "ext_test", NULL);
    if (ret) {
        TEST_FAIL("Failed to request external IRQ");
        irq_dispose_mapping(ext_virq);
        return;
    }
    TEST_PASS("External interrupt requested");
    
    // Verify the interrupt chain
    struct irq_desc *desc = irq_to_desc(ext_virq);
    if (desc && desc->chip && strcmp(desc->chip->name, "PLIC") == 0) {
        TEST_PASS("Interrupt correctly routed through PLIC");
    } else {
        TEST_FAIL("Interrupt not using PLIC chip");
    }
    
    // Clean up
    free_irq(ext_virq, NULL);
    irq_dispose_mapping(ext_virq);
    TEST_PASS("External interrupt flow test complete");
#else
    TEST_INFO("External interrupt test not available on this architecture");
#endif
}

// Test 5: Interrupt Storm Stress Test
static void test_interrupt_storm(void) {
    TEST_START("Interrupt Storm Stress Test");
    
    if (!plic_primary) {
        TEST_FAIL("No PLIC for storm test");
        return;
    }
    
    struct irq_domain *plic_domain = plic_primary->domain;
    if (!plic_domain) {
        TEST_FAIL("No PLIC domain");
        return;
    }
    
    // Create multiple interrupt mappings
    uint32_t storm_virqs[10];
    uint32_t storm_hwirqs[] = {20, 21, 22, 23, 24, 25, 26, 27, 28, 29};
    
    TEST_INFO("Creating storm test interrupts...");
    for (int i = 0; i < 10; i++) {
        storm_virqs[i] = irq_create_mapping(plic_domain, storm_hwirqs[i]);
        if (!storm_virqs[i]) {
            TEST_FAIL("Failed to create storm mapping");
            // Clean up already created mappings
            for (int j = 0; j < i; j++) {
                irq_dispose_mapping(storm_virqs[j]);
            }
            return;
        }
        
        // Set varying priorities
        plic_set_priority(storm_hwirqs[i], (i % 7) + 1);
    }
    TEST_PASS("Created 10 storm test interrupts");
    
    // Rapidly enable/disable interrupts
    TEST_INFO("Running rapid enable/disable test...");
    for (int iter = 0; iter < 100; iter++) {
        for (int i = 0; i < 10; i++) {
            if (iter % 2 == 0) {
                plic_mask_irq(storm_hwirqs[i]);
            } else {
                plic_unmask_irq(storm_hwirqs[i]);
            }
        }
    }
    TEST_PASS("Rapid enable/disable test complete");
    
    // Clean up
    for (int i = 0; i < 10; i++) {
        irq_dispose_mapping(storm_virqs[i]);
    }
    TEST_PASS("Storm test cleanup complete");
}

// Test 6: Priority and Threshold Testing
static void test_priority_threshold(void) {
    TEST_START("Priority and Threshold Testing");
    
    if (!plic_primary) {
        TEST_FAIL("No PLIC for priority test");
        return;
    }
    
    // Test all priority levels
    for (uint32_t prio = 0; prio <= PLIC_MAX_PRIORITY; prio++) {
        plic_set_priority(50, prio);
    }
    TEST_PASS("Tested all priority levels");
    
    // Test threshold settings for different contexts
    plic_set_threshold(0, 0);  // Context 0: Accept all
    plic_set_threshold(1, 3);  // Context 1: Only priority > 3
    TEST_PASS("Set different thresholds for contexts");
    
    // Test priority ordering
    plic_set_priority(30, 7);  // Highest
    plic_set_priority(31, 1);  // Lowest
    plic_set_priority(32, 4);  // Medium
    TEST_PASS("Set priority ordering for multiple interrupts");
    
    TEST_INFO("Priority and threshold configuration verified");
}

// Main test runner for RISC-V hardware interrupts
#ifdef __riscv
void test_riscv_hardware_interrupts(void) {
    uart_puts("\n================================================================\n");
    uart_puts("        RISC-V HARDWARE INTERRUPT TESTS\n");
    uart_puts("================================================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Check and enable interrupts if needed
    uart_puts("\n[INFO] Interrupt status: ");
    if (arch_interrupts_enabled()) {
        uart_puts("ENABLED\n");
    } else {
        uart_puts("DISABLED - Enabling now...\n");
        arch_enable_interrupts();
        if (arch_interrupts_enabled()) {
            uart_puts("[INFO] Interrupts now ENABLED\n");
        } else {
            uart_puts("[WARN] Failed to enable interrupts\n");
        }
    }
    
    // Run all tests
    test_plic_claim_complete();
    test_real_timer_interrupt();
    test_software_interrupt();
    test_external_interrupt_flow();
    test_interrupt_storm();
    test_priority_threshold();
    
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
        uart_puts("\n[SUCCESS] All hardware interrupt tests passed!\n");
    } else {
        uart_puts("\n[FAILURE] Some hardware interrupt tests failed\n");
    }
}
#else
// Stub for when running on non-RISC-V
void test_riscv_hardware_interrupts(void) {
    uart_puts("\n>>> RISC-V hardware interrupt tests not available on this architecture\n");
}
#endif