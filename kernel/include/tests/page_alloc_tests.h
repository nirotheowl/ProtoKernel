/*
 * kernel/include/tests/page_alloc_tests.h
 *
 * Page allocator test suite declarations
 */

#ifndef PAGE_ALLOC_TESTS_H
#define PAGE_ALLOC_TESTS_H

// Run all page allocator tests
void page_alloc_run_tests(void);

// Run production-ready tests
void page_alloc_run_production_tests(void);

// Initialize test framework
void page_alloc_test_init(void);

#endif 