#ifndef MEMORY_TESTS_H
#define MEMORY_TESTS_H

// Comprehensive memory system tests
void run_comprehensive_memory_tests(void);

// Edge case tests
void test_pmm_edge_cases(void);
void test_paging_edge_cases(void);
void test_memmap_edge_cases(void);

// Stress tests
void test_memory_stress(void);
void test_concurrent_allocation(void);
void test_fragmentation_handling(void);

// Validation tests
void test_page_table_consistency(void);
void test_memory_barriers(void);
void test_cache_coherency(void);

#endif // MEMORY_TESTS_H