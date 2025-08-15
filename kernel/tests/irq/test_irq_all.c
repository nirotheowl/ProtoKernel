#include <tests/irq_tests.h>
#include <uart.h>

// Global test statistics
static int total_tests_run = 0;
static int total_tests_passed = 0;
static int total_tests_failed = 0;

// Run all IRQ subsystem tests
void run_all_irq_tests(void) {
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("            COMPREHENSIVE IRQ SUBSYSTEM TEST SUITE\n");
    uart_puts("================================================================\n");
    
    // Run basic tests first
    uart_puts("\n>>> Running basic IRQ tests...\n");
    run_irq_basic_tests();
    
    // Run component-specific comprehensive tests
    uart_puts("\n>>> Running allocator comprehensive tests...\n");
    run_irq_allocator_tests();
    
    uart_puts("\n>>> Running descriptor comprehensive tests...\n");
    run_irq_descriptor_tests();
    
    uart_puts("\n>>> Running domain comprehensive tests...\n");
    run_irq_domain_tests();
    
    // Run stress tests
    uart_puts("\n>>> Running stress tests...\n");
    run_irq_stress_tests();
    
    // Run edge case tests
    uart_puts("\n>>> Running edge case tests...\n");
    run_irq_edge_tests();
    
    // Final summary
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("                    FINAL TEST SUMMARY\n");
    uart_puts("================================================================\n");
    uart_puts("\nAll IRQ subsystem tests completed.\n");
    uart_puts("\nTest Categories:\n");
    uart_puts("  - Basic functionality tests\n");
    uart_puts("  - Allocator comprehensive tests\n");
    uart_puts("  - Descriptor comprehensive tests\n");
    uart_puts("  - Domain comprehensive tests\n");
    uart_puts("  - Stress and performance tests\n");
    uart_puts("  - Edge case and error handling tests\n");
    uart_puts("\n");
    uart_puts("================================================================\n");
}