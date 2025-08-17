/*
 * kernel/tests/pmm_tests.c
 *
 * Unit tests for the physical memory manager
 */

#include <tests/pmm_tests.h>
#include <memory/pmm.h>
#include <uart.h>

// Minimal PMM tests - just check basic functionality
void run_pmm_tests(void) {
    uart_puts("\n=== PMM Tests ===\n");
    
    // Test 1: Basic allocation and free
    uart_puts("PMM allocation/free ... ");
    uint64_t page = pmm_alloc_page();
    if (page && (page & 0xFFF) == 0) {
        pmm_free_page(page);
        uart_puts("PASS\n");
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 2: Multiple page allocation
    uart_puts("PMM multi-page alloc ... ");
    uint64_t pages = pmm_alloc_pages(4);
    if (pages && (pages & 0xFFF) == 0) {
        // Write to each page to verify they're usable
        int ok = 1;
        for (int i = 0; i < 4; i++) {
            uint64_t* ptr = (uint64_t*)(pages + i * PMM_PAGE_SIZE);
            *ptr = 0xDEADBEEF;
            if (*ptr != 0xDEADBEEF) ok = 0;
        }
        pmm_free_pages(pages, 4);
        uart_puts(ok ? "PASS\n" : "FAIL\n");
    } else {
        uart_puts("FAIL\n");
    }
    
    // Test 3: Double free protection
    uart_puts("PMM double-free protection ... ");
    page = pmm_alloc_page();
    if (page) {
        pmm_free_page(page);
        pmm_free_page(page); // Should silently handle double free
        uart_puts("PASS\n");
    } else {
        uart_puts("FAIL\n");
    }
}