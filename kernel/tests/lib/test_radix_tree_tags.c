/*
 * kernel/tests/lib/test_radix_tree_tags.c
 *
 * Tests for radix tree tag operations
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

static void test_tag_single(void) {
    TEST_START("Single tag operations");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *val = (void *)0x1000;
    
    int ret = radix_tree_insert(&tree, 10, val);
    if (ret != 0) {
        TEST_FAIL("Insert failed");
        return;
    }
    
    ret = radix_tree_tag_set(&tree, 10, RADIX_TREE_TAG_ALLOCATED);
    if (ret != 0) {
        TEST_FAIL("Tag set failed");
        return;
    }
    
    ret = radix_tree_tag_get(&tree, 10, RADIX_TREE_TAG_ALLOCATED);
    if (ret != 1) {
        TEST_FAIL("Tag not set");
        return;
    }
    
    ret = radix_tree_tag_get(&tree, 10, RADIX_TREE_TAG_MSI);
    if (ret != 0) {
        TEST_FAIL("Wrong tag set");
        return;
    }
    
    radix_tree_tag_clear(&tree, 10, RADIX_TREE_TAG_ALLOCATED);
    
    ret = radix_tree_tag_get(&tree, 10, RADIX_TREE_TAG_ALLOCATED);
    if (ret != 0) {
        TEST_FAIL("Tag not cleared");
        return;
    }
    
    TEST_PASS();
}

static void test_tag_multiple(void) {
    TEST_START("Multiple tag operations");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    int i;
    
    for (i = 0; i < 20; i++) {
        void *val = (void *)(unsigned long)(0x2000 + i);
        radix_tree_insert(&tree, i * 10, val);
    }
    
    for (i = 0; i < 20; i += 2) {
        radix_tree_tag_set(&tree, i * 10, RADIX_TREE_TAG_ALLOCATED);
    }
    
    for (i = 0; i < 20; i += 3) {
        radix_tree_tag_set(&tree, i * 10, RADIX_TREE_TAG_MSI);
    }
    
    for (i = 0; i < 20; i++) {
        int allocated = radix_tree_tag_get(&tree, i * 10, RADIX_TREE_TAG_ALLOCATED);
        int msi = radix_tree_tag_get(&tree, i * 10, RADIX_TREE_TAG_MSI);
        
        int expected_allocated = (i % 2 == 0) ? 1 : 0;
        int expected_msi = (i % 3 == 0) ? 1 : 0;
        
        if (allocated != expected_allocated) {
            TEST_FAIL("Wrong ALLOCATED tag state");
            return;
        }
        
        if (msi != expected_msi) {
            TEST_FAIL("Wrong MSI tag state");
            return;
        }
    }
    
    TEST_PASS();
}

static void test_tag_nonexistent(void) {
    TEST_START("Tag operations on non-existent entries");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    radix_tree_insert(&tree, 5, (void *)0x3000);
    
    int ret = radix_tree_tag_set(&tree, 10, RADIX_TREE_TAG_ALLOCATED);
    if (ret == 0) {
        TEST_FAIL("Tag set succeeded on non-existent entry");
        return;
    }
    
    ret = radix_tree_tag_get(&tree, 10, RADIX_TREE_TAG_ALLOCATED);
    if (ret != 0) {
        TEST_FAIL("Tag get returned true for non-existent entry");
        return;
    }
    
    radix_tree_tag_clear(&tree, 10, RADIX_TREE_TAG_ALLOCATED);
    
    TEST_PASS();
}

static void test_tag_propagation(void) {
    TEST_START("Tag propagation in tree");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    radix_tree_insert(&tree, 0, (void *)0x4000);
    radix_tree_insert(&tree, 100000, (void *)0x4001);
    radix_tree_insert(&tree, 200000, (void *)0x4002);
    
    radix_tree_tag_set(&tree, 100000, RADIX_TREE_TAG_ALLOCATED);
    
    if (!radix_tree_tag_get(&tree, 100000, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag not set on entry");
        return;
    }
    
    radix_tree_tag_clear(&tree, 100000, RADIX_TREE_TAG_ALLOCATED);
    
    if (radix_tree_tag_get(&tree, 100000, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag not cleared");
        return;
    }
    
    TEST_PASS();
}

static void test_tag_clear_propagation(void) {
    TEST_START("Tag clear propagation");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    radix_tree_insert(&tree, 0, (void *)0x5000);
    radix_tree_insert(&tree, 64, (void *)0x5001);
    radix_tree_insert(&tree, 128, (void *)0x5002);
    
    radix_tree_tag_set(&tree, 0, RADIX_TREE_TAG_MSI);
    radix_tree_tag_set(&tree, 64, RADIX_TREE_TAG_MSI);
    radix_tree_tag_set(&tree, 128, RADIX_TREE_TAG_MSI);
    
    radix_tree_tag_clear(&tree, 0, RADIX_TREE_TAG_MSI);
    radix_tree_tag_clear(&tree, 128, RADIX_TREE_TAG_MSI);
    
    if (!radix_tree_tag_get(&tree, 64, RADIX_TREE_TAG_MSI)) {
        TEST_FAIL("Tag incorrectly cleared on other entry");
        return;
    }
    
    radix_tree_tag_clear(&tree, 64, RADIX_TREE_TAG_MSI);
    
    if (radix_tree_tag_get(&tree, 0, RADIX_TREE_TAG_MSI) ||
        radix_tree_tag_get(&tree, 64, RADIX_TREE_TAG_MSI) ||
        radix_tree_tag_get(&tree, 128, RADIX_TREE_TAG_MSI)) {
        TEST_FAIL("Tags not properly cleared");
        return;
    }
    
    TEST_PASS();
}

static void test_tag_sparse(void) {
    TEST_START("Tags on sparse entries");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t sparse_keys[] = {0, 1000, 100000, 1000000, 0xFFFFFF00};
    int i;
    
    for (i = 0; i < 5; i++) {
        radix_tree_insert(&tree, sparse_keys[i], (void *)(unsigned long)(0x6000 + i));
    }
    
    for (i = 0; i < 5; i++) {
        radix_tree_tag_set(&tree, sparse_keys[i], RADIX_TREE_TAG_ALLOCATED);
    }
    
    for (i = 0; i < 5; i++) {
        if (!radix_tree_tag_get(&tree, sparse_keys[i], RADIX_TREE_TAG_ALLOCATED)) {
            TEST_FAIL("Tag not set on sparse entry");
            return;
        }
    }
    
    for (i = 0; i < 5; i += 2) {
        radix_tree_tag_clear(&tree, sparse_keys[i], RADIX_TREE_TAG_ALLOCATED);
    }
    
    for (i = 0; i < 5; i++) {
        int expected = (i % 2 == 0) ? 0 : 1;
        int got = radix_tree_tag_get(&tree, sparse_keys[i], RADIX_TREE_TAG_ALLOCATED);
        if (got != expected) {
            TEST_FAIL("Wrong tag state after clear");
            return;
        }
    }
    
    TEST_PASS();
}

static void test_tag_after_delete(void) {
    TEST_START("Tags after entry deletion");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    radix_tree_insert(&tree, 10, (void *)0x7000);
    radix_tree_tag_set(&tree, 10, RADIX_TREE_TAG_ALLOCATED);
    
    if (!radix_tree_tag_get(&tree, 10, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag not set");
        return;
    }
    
    radix_tree_delete(&tree, 10);
    
    if (radix_tree_tag_get(&tree, 10, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag still set after delete");
        return;
    }
    
    radix_tree_insert(&tree, 10, (void *)0x7001);
    
    if (radix_tree_tag_get(&tree, 10, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag set on new entry");
        return;
    }
    
    TEST_PASS();
}

static void test_multiple_tags_same_entry(void) {
    TEST_START("Multiple tags on same entry");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    radix_tree_insert(&tree, 42, (void *)0x8000);
    
    radix_tree_tag_set(&tree, 42, RADIX_TREE_TAG_ALLOCATED);
    radix_tree_tag_set(&tree, 42, RADIX_TREE_TAG_MSI);
    
    if (!radix_tree_tag_get(&tree, 42, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("ALLOCATED tag not set");
        return;
    }
    
    if (!radix_tree_tag_get(&tree, 42, RADIX_TREE_TAG_MSI)) {
        TEST_FAIL("MSI tag not set");
        return;
    }
    
    radix_tree_tag_clear(&tree, 42, RADIX_TREE_TAG_ALLOCATED);
    
    if (radix_tree_tag_get(&tree, 42, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("ALLOCATED tag not cleared");
        return;
    }
    
    if (!radix_tree_tag_get(&tree, 42, RADIX_TREE_TAG_MSI)) {
        TEST_FAIL("MSI tag incorrectly cleared");
        return;
    }
    
    TEST_PASS();
}

void test_radix_tree_tags(void) {
    uart_puts("\n=== Radix Tree Tag Tests ===\n");
    
    test_tag_single();
    test_tag_multiple();
    test_tag_nonexistent();
    test_tag_propagation();
    test_tag_clear_propagation();
    test_tag_sparse();
    test_tag_after_delete();
    test_multiple_tags_same_entry();
    
    uart_puts("\n=== Tag Test Summary ===\n");
    uart_puts("Passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nFailed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed > 0) {
        panic("Radix tree tag tests failed!");
    }
}