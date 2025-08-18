/*
 * kernel/tests/lib/test_radix_tree_iter.c
 *
 * Tests for radix tree iteration functionality
 */

#include <lib/radix_tree.h>
#include <string.h>
#include <panic.h>
#include <uart.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) uart_puts("[TEST] " name " ... ")
#define TEST_PASS() do { uart_puts("PASS\n"); tests_passed++; } while(0)
#define TEST_FAIL(msg) do { uart_puts("FAIL: "); uart_puts(msg); uart_puts("\n"); tests_failed++; } while(0)

static void test_iterate_empty(void) {
    TEST_START("Iterate empty tree");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    
    void *entry = radix_tree_next_slot(&tree, &iter, 0);
    if (entry != NULL) {
        TEST_FAIL("Iterator returned non-NULL for empty tree");
        return;
    }
    
    entry = radix_tree_next_tagged(&tree, &iter, 0, RADIX_TREE_TAG_ALLOCATED);
    if (entry != NULL) {
        TEST_FAIL("Tagged iterator returned non-NULL for empty tree");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_sequential(void) {
    TEST_START("Iterate sequential entries");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    int i;
    int count = 0;
    
    for (i = 0; i < 10; i++) {
        radix_tree_insert(&tree, i, (void *)(unsigned long)(0x1000 + i));
    }
    
    void *entry;
    uint32_t index = 0;
    
    while ((entry = radix_tree_next_slot(&tree, &iter, index))) {
        if (iter.index != count) {
            TEST_FAIL("Wrong index in iterator");
            return;
        }
        
        void *expected = (void *)(unsigned long)(0x1000 + count);
        if (entry != expected) {
            TEST_FAIL("Wrong value from iterator");
            return;
        }
        
        count++;
        index = iter.next_index;
    }
    
    if (count != 10) {
        TEST_FAIL("Wrong number of entries iterated");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_sparse(void) {
    TEST_START("Iterate sparse entries");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    uint32_t keys[] = {0, 100, 1000, 10000, 100000};
    int i;
    int count = 0;
    
    for (i = 0; i < 5; i++) {
        radix_tree_insert(&tree, keys[i], (void *)(unsigned long)(0x2000 + i));
    }
    
    void *entry;
    uint32_t index = 0;
    
    while ((entry = radix_tree_next_slot(&tree, &iter, index))) {
        if (iter.index != keys[count]) {
            TEST_FAIL("Wrong index in sparse iteration");
            return;
        }
        
        void *expected = (void *)(unsigned long)(0x2000 + count);
        if (entry != expected) {
            TEST_FAIL("Wrong value in sparse iteration");
            return;
        }
        
        count++;
        index = iter.next_index;
    }
    
    if (count != 5) {
        TEST_FAIL("Wrong number of sparse entries iterated");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_from_middle(void) {
    TEST_START("Iterate from middle");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    int i;
    int count = 0;
    
    for (i = 0; i < 20; i++) {
        radix_tree_insert(&tree, i * 10, (void *)(unsigned long)(0x3000 + i));
    }
    
    void *entry;
    uint32_t index = 75;
    
    entry = radix_tree_next_slot(&tree, &iter, index);
    if (!entry) {
        TEST_FAIL("No entry found starting from middle");
        return;
    }
    
    if (iter.index != 80) {
        TEST_FAIL("Wrong first entry when starting from middle");
        return;
    }
    
    void *expected = (void *)(unsigned long)(0x3000 + 8);
    if (entry != expected) {
        TEST_FAIL("Wrong value when starting from middle");
        return;
    }
    
    index = iter.next_index;
    while ((entry = radix_tree_next_slot(&tree, &iter, index))) {
        count++;
        index = iter.next_index;
    }
    
    if (count != 11) {
        TEST_FAIL("Wrong number of entries after middle");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_tagged_empty(void) {
    TEST_START("Iterate tagged in empty tree");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    
    radix_tree_insert(&tree, 10, (void *)0x4000);
    radix_tree_insert(&tree, 20, (void *)0x4001);
    
    void *entry = radix_tree_next_tagged(&tree, &iter, 0, RADIX_TREE_TAG_ALLOCATED);
    if (entry != NULL) {
        TEST_FAIL("Found tagged entry when none exist");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_tagged_subset(void) {
    TEST_START("Iterate tagged subset");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    int i;
    int count = 0;
    
    for (i = 0; i < 20; i++) {
        radix_tree_insert(&tree, i * 10, (void *)(unsigned long)(0x5000 + i));
    }
    
    for (i = 0; i < 20; i += 3) {
        radix_tree_tag_set(&tree, i * 10, RADIX_TREE_TAG_MSI);
    }
    
    void *entry;
    uint32_t index = 0;
    
    while ((entry = radix_tree_next_tagged(&tree, &iter, index, RADIX_TREE_TAG_MSI))) {
        int expected_idx = count * 3;
        if (iter.index != expected_idx * 10) {
            TEST_FAIL("Wrong tagged index");
            return;
        }
        
        void *expected = (void *)(unsigned long)(0x5000 + expected_idx);
        if (entry != expected) {
            TEST_FAIL("Wrong tagged value");
            return;
        }
        
        count++;
        index = iter.next_index;
    }
    
    if (count != 7) {
        TEST_FAIL("Wrong number of tagged entries");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_tagged_sparse(void) {
    TEST_START("Iterate tagged sparse entries");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    uint32_t keys[] = {0, 1000, 100000, 1000000, 0xFFFF0000};
    int tagged[] = {1, 0, 1, 0, 1};
    int i;
    int count = 0;
    
    for (i = 0; i < 5; i++) {
        radix_tree_insert(&tree, keys[i], (void *)(unsigned long)(0x6000 + i));
        if (tagged[i]) {
            radix_tree_tag_set(&tree, keys[i], RADIX_TREE_TAG_ALLOCATED);
        }
    }
    
    void *entry;
    uint32_t index = 0;
    uint32_t expected_keys[] = {0, 100000, 0xFFFF0000};
    
    while ((entry = radix_tree_next_tagged(&tree, &iter, index, RADIX_TREE_TAG_ALLOCATED))) {
        if (count >= 3) {
            TEST_FAIL("Too many tagged entries found");
            return;
        }
        
        if (iter.index != expected_keys[count]) {
            TEST_FAIL("Wrong sparse tagged index");
            return;
        }
        
        count++;
        index = iter.next_index;
    }
    
    if (count != 3) {
        TEST_FAIL("Wrong number of sparse tagged entries");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_high_keys(void) {
    TEST_START("Iterate high key values");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    uint32_t keys[] = {0xFFFFFFF0, 0xFFFFFFF4, 0xFFFFFFF8, 0xFFFFFFFC};
    int i;
    int count = 0;
    
    for (i = 0; i < 4; i++) {
        radix_tree_insert(&tree, keys[i], (void *)(unsigned long)(0x7000 + i));
    }
    
    void *entry;
    uint32_t index = 0xFFFFFFF0;
    
    while ((entry = radix_tree_next_slot(&tree, &iter, index))) {
        if (iter.index != keys[count]) {
            TEST_FAIL("Wrong high key index");
            return;
        }
        
        void *expected = (void *)(unsigned long)(0x7000 + count);
        if (entry != expected) {
            TEST_FAIL("Wrong high key value");
            return;
        }
        
        count++;
        index = iter.next_index;
    }
    
    if (count != 4) {
        TEST_FAIL("Wrong number of high key entries");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_after_delete(void) {
    TEST_START("Iterate after deletions");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    int i;
    int count = 0;
    
    for (i = 0; i < 10; i++) {
        radix_tree_insert(&tree, i, (void *)(unsigned long)(0x8000 + i));
    }
    
    radix_tree_delete(&tree, 3);
    radix_tree_delete(&tree, 5);
    radix_tree_delete(&tree, 7);
    
    void *entry;
    uint32_t index = 0;
    uint32_t expected_indices[] = {0, 1, 2, 4, 6, 8, 9};
    
    while ((entry = radix_tree_next_slot(&tree, &iter, index))) {
        if (count >= 7) {
            TEST_FAIL("Too many entries after delete");
            return;
        }
        
        if (iter.index != expected_indices[count]) {
            TEST_FAIL("Wrong index after delete");
            return;
        }
        
        count++;
        index = iter.next_index;
    }
    
    if (count != 7) {
        TEST_FAIL("Wrong number of entries after delete");
        return;
    }
    
    TEST_PASS();
}

static void test_iterate_mixed_tags(void) {
    TEST_START("Iterate with mixed tags");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_iter iter;
    int i;
    
    for (i = 0; i < 20; i++) {
        radix_tree_insert(&tree, i, (void *)(unsigned long)(0x9000 + i));
        if (i % 2 == 0) {
            radix_tree_tag_set(&tree, i, RADIX_TREE_TAG_ALLOCATED);
        }
        if (i % 3 == 0) {
            radix_tree_tag_set(&tree, i, RADIX_TREE_TAG_MSI);
        }
    }
    
    int count_allocated = 0;
    void *entry;
    uint32_t index = 0;
    
    while ((entry = radix_tree_next_tagged(&tree, &iter, index, RADIX_TREE_TAG_ALLOCATED))) {
        if (iter.index % 2 != 0) {
            TEST_FAIL("Wrong ALLOCATED tagged entry");
            return;
        }
        count_allocated++;
        index = iter.next_index;
    }
    
    if (count_allocated != 10) {
        TEST_FAIL("Wrong number of ALLOCATED tagged entries");
        return;
    }
    
    int count_msi = 0;
    index = 0;
    
    while ((entry = radix_tree_next_tagged(&tree, &iter, index, RADIX_TREE_TAG_MSI))) {
        if (iter.index % 3 != 0) {
            TEST_FAIL("Wrong MSI tagged entry");
            return;
        }
        count_msi++;
        index = iter.next_index;
    }
    
    if (count_msi != 7) {
        TEST_FAIL("Wrong number of MSI tagged entries");
        return;
    }
    
    TEST_PASS();
}

void test_radix_tree_iter(void) {
    uart_puts("\n=== Radix Tree Iteration Tests ===\n");
    
    test_iterate_empty();
    test_iterate_sequential();
    test_iterate_sparse();
    test_iterate_from_middle();
    test_iterate_tagged_empty();
    test_iterate_tagged_subset();
    test_iterate_tagged_sparse();
    test_iterate_high_keys();
    test_iterate_after_delete();
    test_iterate_mixed_tags();
    
    uart_puts("\n=== Iteration Test Summary ===\n");
    uart_puts("Passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nFailed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed > 0) {
        panic("Radix tree iteration tests failed!");
    }
}