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
    
    // Comment out old tests - focusing on tree domain testing
    // uart_puts("\n>>> Testing Hierarchical Domain Support...\n");
    test_hierarchical_domains();
    
    // uart_puts("\n>>> Testing Radix Tree (for sparse domains)...\n");
    test_radix_tree_all();
    
    // Run comprehensive tree domain tests
    uart_puts("\n>>> Testing Tree Domain Support...\n");
    test_tree_domains_comprehensive();
    
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("                    ALL TESTS COMPLETE\n");
    uart_puts("================================================================\n");
}
