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
    uart_puts("            IRQ SUBSYSTEM TESTS\n");
    uart_puts("================================================================\n");
    
    // Run hierarchical domain tests (architecture-independent)
    uart_puts("\n>>> Testing Hierarchical Domain Support...\n");
    test_hierarchical_domains();
    
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("                    ALL TESTS COMPLETE\n");
    uart_puts("================================================================\n");
}