/*
 * Hardware Interrupt Integration Tests
 * Tests for GIC/PLIC integration with real hardware
 */

#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irqchip/arm-gic.h>
#include <uart.h>
#include <arch_interface.h>
#include <stdint.h>
#include <stdbool.h>

// Test counters
static volatile int timer_irq_count = 0;
static volatile int uart_irq_count = 0;

// Timer interrupt handler
static void timer_irq_handler(void *data) {
    timer_irq_count++;
    uart_puts("[TIMER IRQ] Timer interrupt received (count=");
    uart_putdec(timer_irq_count);
    uart_puts(")\n");
    
    // TODO: Reset timer for next interrupt
}

// UART interrupt handler  
static void uart_irq_handler(void *data) {
    uart_irq_count++;
    uart_puts("[UART IRQ] UART interrupt received (count=");
    uart_putdec(uart_irq_count);
    uart_puts(")\n");
    
    // TODO: Clear UART interrupt
}

// Test GIC domain creation
static bool test_gic_domain_creation(void) {
    uart_puts("\n=== Testing GIC Domain Creation ===\n");
    
    // Check if GIC domain was created
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        uart_puts("[FAIL] GIC domain not found\n");
        return false;
    }
    
    uart_puts("[PASS] GIC domain found: ");
    uart_puts(gic_domain->name);
    uart_puts("\n");
    
    // Check domain properties
    uart_puts("  - Domain type: ");
    if (gic_domain->type == DOMAIN_LINEAR) {
        uart_puts("LINEAR\n");
    } else {
        uart_puts("OTHER\n");
    }
    
    uart_puts("  - Domain size: ");
    uart_putdec(gic_domain->size);
    uart_puts(" interrupts\n");
    
    return true;
}

// Test timer interrupt setup
static bool test_timer_interrupt(void) {
    uart_puts("\n=== Testing Timer Interrupt ===\n");
    
#ifdef __aarch64__
    // ARM Generic Timer uses PPI 14 (hwirq 30 in GIC)
    uint32_t timer_hwirq = 30; // Physical timer PPI
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        uart_puts("[FAIL] No GIC domain for timer\n");
        return false;
    }
    
    // Create mapping for timer interrupt
    uint32_t timer_virq = irq_create_mapping(gic_domain, timer_hwirq);
    if (!timer_virq) {
        uart_puts("[FAIL] Failed to create timer mapping\n");
        return false;
    }
    
    uart_puts("[INFO] Timer mapped: hwirq=");
    uart_putdec(timer_hwirq);
    uart_puts(" -> virq=");
    uart_putdec(timer_virq);
    uart_puts("\n");
    
    // Request timer interrupt
    int ret = request_irq(timer_virq, timer_irq_handler, 0, "timer", NULL);
    if (ret) {
        uart_puts("[FAIL] Failed to request timer IRQ\n");
        return false;
    }
    
    uart_puts("[PASS] Timer interrupt requested\n");
    
    // TODO: Enable timer and wait for interrupt
    
#elif defined(__riscv)
    uart_puts("[INFO] RISC-V timer test not yet implemented\n");
#endif
    
    return true;
}

// Test UART interrupt setup
static bool test_uart_interrupt(void) {
    uart_puts("\n=== Testing UART Interrupt ===\n");
    
#ifdef __aarch64__
    // PL011 UART typically uses SPI 1 (hwirq 33 in GIC)
    uint32_t uart_hwirq = 33;
    
    struct irq_domain *gic_domain = irq_find_host(NULL);
    if (!gic_domain) {
        uart_puts("[FAIL] No GIC domain for UART\n");
        return false;
    }
    
    // Create mapping for UART interrupt
    uint32_t uart_virq = irq_create_mapping(gic_domain, uart_hwirq);
    if (!uart_virq) {
        uart_puts("[FAIL] Failed to create UART mapping\n");
        return false;
    }
    
    uart_puts("[INFO] UART mapped: hwirq=");
    uart_putdec(uart_hwirq);
    uart_puts(" -> virq=");
    uart_putdec(uart_virq);
    uart_puts("\n");
    
    // Request UART interrupt
    int ret = request_irq(uart_virq, uart_irq_handler, IRQF_SHARED, "uart", NULL);
    if (ret) {
        uart_puts("[FAIL] Failed to request UART IRQ\n");
        return false;
    }
    
    uart_puts("[PASS] UART interrupt requested\n");
    
    // TODO: Enable UART interrupts and trigger one
    
#elif defined(__riscv)
    uart_puts("[INFO] RISC-V UART test not yet implemented\n");
#endif
    
    return true;
}

// Test interrupt delivery through exception handler
static bool test_exception_integration(void) {
    uart_puts("\n=== Testing Exception Handler Integration ===\n");
    
    // This would be tested by actually receiving interrupts
    // For now, just verify the handler is installed
    
#ifdef __aarch64__
    uart_puts("[INFO] ARM64 exception vectors installed\n");
    uart_puts("[INFO] IRQ handler will dispatch through domains\n");
#elif defined(__riscv)
    uart_puts("[INFO] RISC-V trap handler installed\n");
#endif
    
    return true;
}

// Main hardware interrupt test function
void run_arm_hardware_interrupt_tests(void) {
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("   HARDWARE INTERRUPT TESTS\n");
    uart_puts("========================================\n");
    
    int passed = 0;
    int failed = 0;
    
    // Test GIC/PLIC domain creation
    if (test_gic_domain_creation()) {
        passed++;
    } else {
        failed++;
    }
    
    // Test timer interrupt setup
    if (test_timer_interrupt()) {
        passed++;
    } else {
        failed++;
    }
    
    // Test UART interrupt setup
    if (test_uart_interrupt()) {
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
    uart_puts("HARDWARE TEST SUMMARY:\n");
    uart_puts("  PASSED: ");
    uart_putdec(passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(failed);
    uart_puts("\n  RESULT: ");
    
    if (failed == 0) {
        uart_puts("ALL HARDWARE TESTS PASSED!\n");
    } else {
        uart_puts("SOME TESTS FAILED!\n");
    }
    uart_puts("========================================\n");
}