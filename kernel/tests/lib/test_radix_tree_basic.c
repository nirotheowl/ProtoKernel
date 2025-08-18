/*
 * kernel/tests/lib/test_radix_tree_basic.c
 *
 * Basic tests for radix tree implementation
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

static void test_empty_tree(void) {
    TEST_START("Empty tree operations");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    if (!radix_tree_empty(&tree)) {
        TEST_FAIL("New tree not empty");
        return;
    }
    
    void *val = radix_tree_lookup(&tree, 0);
    if (val != NULL) {
        TEST_FAIL("Lookup in empty tree returned non-NULL");
        return;
    }
    
    val = radix_tree_delete(&tree, 0);
    if (val != NULL) {
        TEST_FAIL("Delete from empty tree returned non-NULL");
        return;
    }
    
    TEST_PASS();
}

static void test_single_insert(void) {
    TEST_START("Single insert");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *test_val = (void *)0x1234;
    
    int ret = radix_tree_insert(&tree, 0, test_val);
    if (ret != 0) {
        TEST_FAIL("Insert failed");
        return;
    }
    
    if (radix_tree_empty(&tree)) {
        TEST_FAIL("Tree empty after insert");
        return;
    }
    
    void *val = radix_tree_lookup(&tree, 0);
    if (val != test_val) {
        TEST_FAIL("Lookup returned wrong value");
        return;
    }
    
    TEST_PASS();
}

static void test_multiple_insert(void) {
    TEST_START("Multiple inserts");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    int i;
    
    for (i = 0; i < 10; i++) {
        void *val = (void *)(unsigned long)(0x1000 + i);
        int ret = radix_tree_insert(&tree, i, val);
        if (ret != 0) {
            TEST_FAIL("Insert failed");
            return;
        }
    }
    
    for (i = 0; i < 10; i++) {
        void *expected = (void *)(unsigned long)(0x1000 + i);
        void *val = radix_tree_lookup(&tree, i);
        if (val != expected) {
            TEST_FAIL("Lookup returned wrong value");
            return;
        }
    }
    
    TEST_PASS();
}

static void test_sparse_insert(void) {
    TEST_START("Sparse inserts");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t keys[] = {0, 100, 1000, 10000, 100000, 1000000};
    int i;
    
    for (i = 0; i < 6; i++) {
        void *val = (void *)(unsigned long)(0x2000 + i);
        int ret = radix_tree_insert(&tree, keys[i], val);
        if (ret != 0) {
            TEST_FAIL("Sparse insert failed");
            return;
        }
    }
    
    for (i = 0; i < 6; i++) {
        void *expected = (void *)(unsigned long)(0x2000 + i);
        void *val = radix_tree_lookup(&tree, keys[i]);
        if (val != expected) {
            TEST_FAIL("Sparse lookup failed");
            return;
        }
    }
    
    void *val = radix_tree_lookup(&tree, 50);
    if (val != NULL) {
        TEST_FAIL("Lookup of non-existent key returned non-NULL");
        return;
    }
    
    TEST_PASS();
}

static void test_delete_single(void) {
    TEST_START("Single delete");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *test_val = (void *)0x3000;
    
    radix_tree_insert(&tree, 5, test_val);
    
    void *val = radix_tree_delete(&tree, 5);
    if (val != test_val) {
        TEST_FAIL("Delete returned wrong value");
        return;
    }
    
    val = radix_tree_lookup(&tree, 5);
    if (val != NULL) {
        TEST_FAIL("Lookup after delete returned non-NULL");
        return;
    }
    
    if (!radix_tree_empty(&tree)) {
        TEST_FAIL("Tree not empty after deleting only element");
        return;
    }
    
    TEST_PASS();
}

static void test_delete_multiple(void) {
    TEST_START("Multiple deletes");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    int i;
    
    for (i = 0; i < 20; i++) {
        void *val = (void *)(unsigned long)(0x4000 + i);
        radix_tree_insert(&tree, i, val);
    }
    
    for (i = 0; i < 20; i += 2) {
        void *expected = (void *)(unsigned long)(0x4000 + i);
        void *val = radix_tree_delete(&tree, i);
        if (val != expected) {
            TEST_FAIL("Delete returned wrong value");
            return;
        }
    }
    
    for (i = 0; i < 20; i++) {
        void *val = radix_tree_lookup(&tree, i);
        if (i % 2 == 0) {
            if (val != NULL) {
                TEST_FAIL("Deleted entry still present");
                return;
            }
        } else {
            void *expected = (void *)(unsigned long)(0x4000 + i);
            if (val != expected) {
                TEST_FAIL("Non-deleted entry has wrong value");
                return;
            }
        }
    }
    
    TEST_PASS();
}

static void test_replace(void) {
    TEST_START("Replace operation");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *old_val = (void *)0x5000;
    void *new_val = (void *)0x6000;
    
    radix_tree_insert(&tree, 42, old_val);
    
    void *ret = radix_tree_replace(&tree, 42, new_val);
    if (ret != old_val) {
        TEST_FAIL("Replace returned wrong old value");
        return;
    }
    
    void *val = radix_tree_lookup(&tree, 42);
    if (val != new_val) {
        TEST_FAIL("Lookup after replace returned wrong value");
        return;
    }
    
    ret = radix_tree_replace(&tree, 99, new_val);
    if (ret != NULL) {
        TEST_FAIL("Replace on non-existent key returned non-NULL");
        return;
    }
    
    TEST_PASS();
}

static void test_high_keys(void) {
    TEST_START("High key values");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t high_keys[] = {
        0xFFFFFFFF,
        0xFFFFFFF0,
        0x80000000,
        0x7FFFFFFF
    };
    int i;
    
    for (i = 0; i < 4; i++) {
        void *val = (void *)(unsigned long)(0x7000 + i);
        int ret = radix_tree_insert(&tree, high_keys[i], val);
        if (ret != 0) {
            TEST_FAIL("High key insert failed");
            return;
        }
    }
    
    for (i = 0; i < 4; i++) {
        void *expected = (void *)(unsigned long)(0x7000 + i);
        void *val = radix_tree_lookup(&tree, high_keys[i]);
        if (val != expected) {
            TEST_FAIL("High key lookup failed");
            return;
        }
    }
    
    TEST_PASS();
}

static void test_duplicate_insert(void) {
    TEST_START("Duplicate insert rejection");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *val1 = (void *)0x8000;
    void *val2 = (void *)0x9000;
    
    int ret = radix_tree_insert(&tree, 10, val1);
    if (ret != 0) {
        TEST_FAIL("First insert failed");
        return;
    }
    
    ret = radix_tree_insert(&tree, 10, val2);
    if (ret == 0) {
        TEST_FAIL("Duplicate insert succeeded");
        return;
    }
    
    void *val = radix_tree_lookup(&tree, 10);
    if (val != val1) {
        TEST_FAIL("Value changed after failed duplicate insert");
        return;
    }
    
    TEST_PASS();
}

static void test_stats(void) {
    TEST_START("Statistics tracking");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    struct radix_tree_stats stats;
    int i;
    
    radix_tree_get_stats(&tree, &stats);
    if (stats.entries != 0 || stats.nodes != 0) {
        TEST_FAIL("Empty tree has non-zero stats");
        return;
    }
    
    for (i = 0; i < 100; i++) {
        void *val = (void *)(unsigned long)(i + 1);  // Avoid NULL for i=0
        int ret = radix_tree_insert(&tree, i * 10, val);
        if (ret != 0) {
            TEST_FAIL("Failed to insert value");
            return;
        }
    }
    
    radix_tree_get_stats(&tree, &stats);
    
    if (stats.entries != 100) {
        TEST_FAIL("Wrong entry count");
        return;
    }
    
    if (stats.nodes == 0) {
        TEST_FAIL("No nodes allocated");
        return;
    }
    
    if (stats.memory_usage == 0) {
        TEST_FAIL("No memory usage reported");
        return;
    }
    
    TEST_PASS();
}

void test_radix_tree_basic(void) {
    uart_puts("\n=== Basic Radix Tree Tests ===\n");
    
    radix_tree_node_cache_init();
    
    test_empty_tree();
    test_single_insert();
    test_multiple_insert();
    test_sparse_insert();
    test_delete_single();
    test_delete_multiple();
    test_replace();
    test_high_keys();
    test_duplicate_insert();
    test_stats();
    
    uart_puts("\n=== Test Summary ===\n");
    uart_puts("Passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nFailed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed > 0) {
        panic("Radix tree tests failed!");
    }
}