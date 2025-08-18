/*
 * kernel/tests/lib/test_radix_tree_all.c
 *
 * Master test runner for all radix tree tests
 */

#include <tests/radix_tree_tests.h>
#include <lib/radix_tree.h>
#include <uart.h>
#include <panic.h>

void test_radix_tree_all(void) {
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("     RADIX TREE COMPREHENSIVE TESTS    \n");
    uart_puts("========================================\n");
    
    radix_tree_node_cache_init();
    
    uart_puts("\nBasic Operations\n");
    uart_puts("----------------\n");
    test_radix_tree_basic();
    
    uart_puts("\nTag Operations\n");
    uart_puts("--------------\n");
    test_radix_tree_tags();
    
    uart_puts("\nIteration\n");
    uart_puts("---------\n");
    test_radix_tree_iter();
    
    uart_puts("\nStress & Bulk Operations\n");
    uart_puts("------------------------\n");
    test_radix_tree_stress();
    
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("   ALL RADIX TREE TESTS COMPLETED!     \n");
    uart_puts("========================================\n");
    uart_puts("\n");
}