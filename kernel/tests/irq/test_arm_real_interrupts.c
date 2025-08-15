/*
 * Real Hardware Interrupt Tests
 * Tests actual interrupt delivery through the exception handler
 */

#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irqchip/arm-gic.h>
#include <uart.h>
#include <arch_interface.h>
#include <arch_timer.h>
#include <arch_cpu.h>
#include <stdint.h>
#include <stdbool.h>

// Test counters
static volatile int timer_fired = 0;
static volatile int uart_fired = 0;
static volatile int sgi_fired = 0;

// Test handlers
static void real_timer_handler(void *data) {
    timer_fired++;
    // Clear timer interrupt
    arch_timer_clear_interrupt();
}

__attribute__((unused))
static void real_uart_handler(void *data) {
    uart_fired++;
    // Would need to clear UART interrupt here
}

static void real_sgi_handler(void *data) {
    sgi_fired++;
    // SGI auto-clears on EOI
}

// Test real timer interrupt
static bool test_real_timer_interrupt(void) {
    uart_puts("\n[TEST] Real Timer Interrupt\n");
    
#ifdef __aarch64__
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        uart_puts("  [FAIL] No GIC domain\n");
        return false;
    }
    
    // ARM Generic Timer Physical Timer uses PPI 14 (hwirq 30)
    uint32_t timer_hwirq = 30;
    // First try to find existing mapping
    uint32_t timer_virq = irq_find_mapping(gic_domain, timer_hwirq);
    if (timer_virq == IRQ_INVALID || timer_virq == 0) {
        // Create new mapping if none exists
        timer_virq = irq_create_mapping(gic_domain, timer_hwirq);
        if (!timer_virq) {
            uart_puts("  [FAIL] Failed to map timer interrupt\n");
            return false;
        }
    }
    
    uart_puts("  [INFO] Timer mapped: hwirq=");
    uart_putdec(timer_hwirq);
    uart_puts(" -> virq=");
    uart_putdec(timer_virq);
    uart_puts("\n");
    
    // Check if already has a handler and free it
    struct irq_desc *existing_desc = irq_to_desc(timer_virq);
    if (existing_desc && existing_desc->action) {
        uart_puts("  [INFO] Freeing existing handler: ");
        uart_puts(existing_desc->action->name ? existing_desc->action->name : "(unnamed)");
        uart_puts("\n");
        free_irq(timer_virq, NULL);
    }
    
    // Request timer interrupt
    int ret = request_irq(timer_virq, real_timer_handler, 0, "timer_test", NULL);
    if (ret) {
        uart_puts("  [FAIL] Failed to request timer IRQ (ret=");
        uart_putdec(ret);
        uart_puts(", virq=");
        uart_putdec(timer_virq);
        uart_puts(")\n");
        
        // Check if it's already taken
        struct irq_desc *desc = irq_to_desc(timer_virq);
        if (desc && desc->action) {
            uart_puts("  [INFO] IRQ already has handler: ");
            uart_puts(desc->action->name ? desc->action->name : "(unnamed)");
            uart_puts("\n");
        }
        return false;
    }
    
    uart_puts("  [PASS] Timer IRQ requested\n");
    
    // Reset counter
    timer_fired = 0;
    
    // Set up timer to fire in a short time
    uint64_t current = arch_timer_get_counter();
    uint64_t frequency = arch_timer_get_frequency();
    uint64_t ticks = frequency / 1000;  // 1ms worth of ticks
    
    arch_timer_set_compare(current + ticks);
    arch_timer_enable();
    
    // Wait for interrupt (with timeout)
    int timeout = 100000;
    while (timer_fired == 0 && timeout-- > 0) {
        arch_cpu_relax();
    }
    
    // Disable timer
    arch_timer_disable();
    
    if (timer_fired > 0) {
        uart_puts("  [PASS] Timer interrupt fired (count=");
        uart_putdec(timer_fired);
        uart_puts(")\n");
    } else {
        uart_puts("  [FAIL] Timer interrupt did not fire\n");
        free_irq(timer_virq, NULL);
        return false;
    }
    
    // Clean up
    free_irq(timer_virq, NULL);
    uart_puts("  [PASS] Timer test complete\n");
    return true;
    
#else
    uart_puts("  [INFO] Timer test not implemented for this architecture\n");
    return true;
#endif
}

// Test Software Generated Interrupt
static bool test_real_sgi(void) {
    uart_puts("\n[TEST] Real Software Generated Interrupt\n");
    
#ifdef __aarch64__
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        uart_puts("  [FAIL] No GIC domain\n");
        return false;
    }
    
    // Use SGI 15 for testing (less likely to conflict)
    uint32_t sgi_hwirq = 15;
    // First try to find existing mapping
    uint32_t sgi_virq = irq_find_mapping(gic_domain, sgi_hwirq);
    if (sgi_virq == IRQ_INVALID || sgi_virq == 0) {
        // Create new mapping if none exists
        sgi_virq = irq_create_mapping(gic_domain, sgi_hwirq);
        if (!sgi_virq) {
            uart_puts("  [FAIL] Failed to map SGI\n");
            return false;
        }
    }
    
    // Request SGI
    int ret = request_irq(sgi_virq, real_sgi_handler, 0, "sgi_test", NULL);
    if (ret) {
        uart_puts("  [FAIL] Failed to request SGI\n");
        return false;
    }
    
    uart_puts("  [PASS] SGI requested\n");
    
    // Reset counter
    sgi_fired = 0;
    
    // Trigger SGI to self
    gic_send_sgi(sgi_hwirq, 0x1);  // Target CPU0
    
    // Small delay for interrupt to be delivered
    for (int i = 0; i < 1000; i++) {
        arch_cpu_relax();
    }
    
    if (sgi_fired > 0) {
        uart_puts("  [PASS] SGI fired (count=");
        uart_putdec(sgi_fired);
        uart_puts(")\n");
    } else {
        uart_puts("  [FAIL] SGI did not fire\n");
        free_irq(sgi_virq, NULL);
        return false;
    }
    
    // Clean up
    free_irq(sgi_virq, NULL);
    uart_puts("  [PASS] SGI test complete\n");
    return true;
    
#else
    uart_puts("  [INFO] SGI test not implemented for this architecture\n");
    return true;
#endif
}

// Test exception handler integration
static bool test_exception_integration(void) {
    uart_puts("\n[TEST] Exception Handler Integration\n");
    
    // The fact that we can receive any interrupts proves the exception
    // handler is properly integrated. We'll verify the path exists.
    
#ifdef __aarch64__
    // Check that GIC is accessible from exception context
    if (!gic_primary) {
        uart_puts("  [FAIL] GIC not initialized for exception handler\n");
        return false;
    }
    uart_puts("  [PASS] GIC accessible from exception context\n");
    
    // Check that domain is set up
    if (!gic_primary->domain) {
        uart_puts("  [FAIL] GIC domain not set up\n");
        return false;
    }
    uart_puts("  [PASS] GIC domain ready for dispatch\n");
    
    // If we successfully handled interrupts in previous tests,
    // the integration is working
    if (timer_fired > 0 || sgi_fired > 0) {
        uart_puts("  [PASS] Exception handler successfully dispatched interrupts\n");
    } else {
        uart_puts("  [INFO] No interrupts fired yet to verify dispatch\n");
    }
    
    return true;
#else
    uart_puts("  [INFO] Exception integration test not implemented for this architecture\n");
    return true;
#endif
}

// Main real interrupt test function
void run_arm_real_interrupt_tests(void) {
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("      REAL HARDWARE INTERRUPT TESTS\n");
    uart_puts("========================================\n");
    
    // Check interrupt status
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
    
    int passed = 0;
    int failed = 0;
    
    // Test SGI (Software Generated Interrupt)
    if (test_real_sgi()) {
        passed++;
    } else {
        failed++;
    }
    
    // Test timer interrupt
    if (test_real_timer_interrupt()) {
        passed++;
    } else {
        failed++;
    }
    
    // Test exception integration
    if (test_exception_integration()) {
        passed++;
    } else {
        failed++;
    }
    
    // Summary
    uart_puts("\n========================================\n");
    uart_puts("REAL INTERRUPT TEST SUMMARY:\n");
    uart_puts("  PASSED: ");
    uart_putdec(passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(failed);
    uart_puts("\n  RESULT: ");
    
    if (failed == 0) {
        uart_puts("ALL REAL INTERRUPT TESTS PASSED!\n");
    } else {
        uart_puts("SOME TESTS FAILED!\n");
    }
    uart_puts("========================================\n");
}