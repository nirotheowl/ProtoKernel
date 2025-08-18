/*
 * kernel/tests/lib/test_radix_tree_edge.c
 *
 * Edge case tests for radix tree implementation
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

static void test_replace_preserves_tags(void) {
    TEST_START("Replace preserves tags");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *old_val = (void *)0x1000;
    void *new_val = (void *)0x2000;
    
    radix_tree_insert(&tree, 100, old_val);
    radix_tree_tag_set(&tree, 100, RADIX_TREE_TAG_ALLOCATED);
    radix_tree_tag_set(&tree, 100, RADIX_TREE_TAG_MSI);
    
    void *ret = radix_tree_replace(&tree, 100, new_val);
    if (ret != old_val) {
        TEST_FAIL("Replace returned wrong value");
        return;
    }
    
    // Check if tags are still set after replace
    if (!radix_tree_tag_get(&tree, 100, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("ALLOCATED tag lost after replace");
        return;
    }
    
    if (!radix_tree_tag_get(&tree, 100, RADIX_TREE_TAG_MSI)) {
        TEST_FAIL("MSI tag lost after replace");
        return;
    }
    
    TEST_PASS();
}

static void test_tree_shrink_with_tags(void) {
    TEST_START("Tree shrink preserves tags");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    // Insert entries that will cause tree to grow
    radix_tree_insert(&tree, 0, (void *)0x1000);
    radix_tree_insert(&tree, 0xFFFFFFFF, (void *)0x2000);
    
    // Tag both entries
    radix_tree_tag_set(&tree, 0, RADIX_TREE_TAG_ALLOCATED);
    radix_tree_tag_set(&tree, 0xFFFFFFFF, RADIX_TREE_TAG_ALLOCATED);
    
    // Delete the high entry, tree should shrink
    radix_tree_delete(&tree, 0xFFFFFFFF);
    
    // Check if tag on remaining entry is still set
    if (!radix_tree_tag_get(&tree, 0, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag lost after tree shrink");
        return;
    }
    
    // Check that we can still find it via tagged iteration
    struct radix_tree_iter iter;
    void *entry = radix_tree_next_tagged(&tree, &iter, 0, RADIX_TREE_TAG_ALLOCATED);
    if (!entry || iter.index != 0) {
        TEST_FAIL("Tagged iteration failed after shrink");
        return;
    }
    
    TEST_PASS();
}

static void test_adjacent_keys_with_tags(void) {
    TEST_START("Adjacent keys with different tags");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    int i;
    
    // Insert adjacent keys with alternating tags
    for (i = 0; i < 64; i++) {
        radix_tree_insert(&tree, i, (void *)(unsigned long)(0x1000 + i));
        if (i % 2 == 0) {
            radix_tree_tag_set(&tree, i, RADIX_TREE_TAG_ALLOCATED);
        } else {
            radix_tree_tag_set(&tree, i, RADIX_TREE_TAG_MSI);
        }
    }
    
    // Verify tags are correct
    for (i = 0; i < 64; i++) {
        int has_allocated = radix_tree_tag_get(&tree, i, RADIX_TREE_TAG_ALLOCATED);
        int has_msi = radix_tree_tag_get(&tree, i, RADIX_TREE_TAG_MSI);
        
        if (i % 2 == 0) {
            if (!has_allocated || has_msi) {
                TEST_FAIL("Wrong tags on even entry");
                return;
            }
        } else {
            if (has_allocated || !has_msi) {
                TEST_FAIL("Wrong tags on odd entry");
                return;
            }
        }
    }
    
    // Clear some tags and verify neighbors aren't affected
    for (i = 0; i < 64; i += 4) {
        radix_tree_tag_clear(&tree, i, RADIX_TREE_TAG_ALLOCATED);
    }
    
    for (i = 0; i < 64; i++) {
        int has_allocated = radix_tree_tag_get(&tree, i, RADIX_TREE_TAG_ALLOCATED);
        if (i % 2 == 0 && i % 4 != 0) {
            if (!has_allocated) {
                TEST_FAIL("Tag incorrectly cleared on neighbor");
                return;
            }
        }
    }
    
    TEST_PASS();
}

static void test_power_of_2_boundaries(void) {
    TEST_START("Power of 2 boundary keys");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t keys[] = {
        63, 64, 65,           // Around 2^6
        4095, 4096, 4097,     // Around 2^12
        262143, 262144, 262145, // Around 2^18
        16777215, 16777216, 16777217 // Around 2^24
    };
    int i;
    
    for (i = 0; i < 12; i++) {
        int ret = radix_tree_insert(&tree, keys[i], (void *)(unsigned long)(0x1000 + i));
        if (ret != 0) {
            TEST_FAIL("Failed to insert boundary key");
            return;
        }
        radix_tree_tag_set(&tree, keys[i], RADIX_TREE_TAG_ALLOCATED);
    }
    
    // Verify all can be found
    for (i = 0; i < 12; i++) {
        void *val = radix_tree_lookup(&tree, keys[i]);
        if (val != (void *)(unsigned long)(0x1000 + i)) {
            TEST_FAIL("Failed to lookup boundary key");
            return;
        }
        
        if (!radix_tree_tag_get(&tree, keys[i], RADIX_TREE_TAG_ALLOCATED)) {
            TEST_FAIL("Tag not set on boundary key");
            return;
        }
    }
    
    // Verify iteration finds all
    struct radix_tree_iter iter;
    uint32_t index = 0;
    int count = 0;
    void *entry;
    
    while ((entry = radix_tree_next_tagged(&tree, &iter, index, RADIX_TREE_TAG_ALLOCATED))) {
        count++;
        index = iter.next_index;
    }
    
    if (count != 12) {
        TEST_FAIL("Wrong count in boundary iteration");
        return;
    }
    
    TEST_PASS();
}

static void test_delete_during_iteration(void) {
    TEST_START("Delete during iteration scenario");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    int i;
    
    // Insert entries
    for (i = 0; i < 20; i++) {
        radix_tree_insert(&tree, i * 10, (void *)(unsigned long)(0x1000 + i));
        radix_tree_tag_set(&tree, i * 10, RADIX_TREE_TAG_ALLOCATED);
    }
    
    // Simulate iteration with deletion
    struct radix_tree_iter iter;
    uint32_t index = 0;
    int count = 0;
    
    // First iteration: count entries
    while (radix_tree_next_tagged(&tree, &iter, index, RADIX_TREE_TAG_ALLOCATED)) {
        count++;
        index = iter.next_index;
        
        // Delete some entries "during" iteration
        if (count == 5) {
            radix_tree_delete(&tree, 150); // Delete future entry
            radix_tree_delete(&tree, 30);  // Delete past entry
        }
    }
    
    // We deleted entry 150 (index 15) which is after current position,
    // so we should find 19 entries (all except the deleted future one)
    if (count != 19) {
        TEST_FAIL("Wrong count during iteration with deletion");
        return;
    }
    
    // Second iteration should find 18
    index = 0;
    count = 0;
    while (radix_tree_next_tagged(&tree, &iter, index, RADIX_TREE_TAG_ALLOCATED)) {
        count++;
        index = iter.next_index;
    }
    
    if (count != 18) {
        TEST_FAIL("Wrong count after deletions");
        return;
    }
    
    TEST_PASS();
}

static void test_zero_key_comprehensive(void) {
    TEST_START("Zero key comprehensive");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    // Test zero as only entry
    radix_tree_insert(&tree, 0, (void *)0x1000);
    radix_tree_tag_set(&tree, 0, RADIX_TREE_TAG_ALLOCATED);
    
    void *val = radix_tree_lookup(&tree, 0);
    if (val != (void *)0x1000) {
        TEST_FAIL("Zero key lookup failed");
        return;
    }
    
    if (!radix_tree_tag_get(&tree, 0, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Zero key tag failed");
        return;
    }
    
    // Add more entries and verify zero still works
    radix_tree_insert(&tree, 1, (void *)0x1001);
    radix_tree_insert(&tree, 0xFFFFFFFF, (void *)0x1002);
    
    val = radix_tree_lookup(&tree, 0);
    if (val != (void *)0x1000) {
        TEST_FAIL("Zero key lookup failed after tree growth");
        return;
    }
    
    // Test iteration starting from 0
    struct radix_tree_iter iter;
    void *entry = radix_tree_next_slot(&tree, &iter, 0);
    if (!entry || iter.index != 0) {
        TEST_FAIL("Iteration from 0 failed");
        return;
    }
    
    // Delete zero and verify
    val = radix_tree_delete(&tree, 0);
    if (val != (void *)0x1000) {
        TEST_FAIL("Zero key delete returned wrong value");
        return;
    }
    
    val = radix_tree_lookup(&tree, 0);
    if (val != NULL) {
        TEST_FAIL("Zero key still present after delete");
        return;
    }
    
    TEST_PASS();
}

static void test_gang_lookup_sparse(void) {
    TEST_START("Gang lookup with very sparse entries");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t keys[] = {0, 1000000, 2000000, 3000000, 4000000, 0xF0000000};
    void *results[10];
    int i;
    
    for (i = 0; i < 6; i++) {
        radix_tree_insert(&tree, keys[i], (void *)(unsigned long)(0x1000 + i));
    }
    
    // Gang lookup from 0
    unsigned int count = radix_tree_gang_lookup(&tree, results, 0, 10);
    if (count != 6) {
        TEST_FAIL("Wrong count from sparse gang lookup");
        return;
    }
    
    for (i = 0; i < 6; i++) {
        if (results[i] != (void *)(unsigned long)(0x1000 + i)) {
            TEST_FAIL("Wrong value from sparse gang lookup");
            return;
        }
    }
    
    // Gang lookup from middle
    count = radix_tree_gang_lookup(&tree, results, 1500000, 3);
    if (count != 3) {
        TEST_FAIL("Wrong count from middle sparse gang lookup");
        return;
    }
    
    // Gang lookup from beyond last
    count = radix_tree_gang_lookup(&tree, results, 0xF0000001, 10);
    if (count != 0) {
        TEST_FAIL("Gang lookup beyond last returned entries");
        return;
    }
    
    TEST_PASS();
}

static void test_tag_propagation_complex(void) {
    TEST_START("Complex tag propagation patterns");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    // Create a pattern where multiple entries share parent nodes
    // These will be in the same leaf node (all have same higher bits)
    radix_tree_insert(&tree, 0x100, (void *)0x1000);
    radix_tree_insert(&tree, 0x101, (void *)0x1001);
    radix_tree_insert(&tree, 0x102, (void *)0x1002);
    radix_tree_insert(&tree, 0x103, (void *)0x1003);
    
    // Tag some but not all
    radix_tree_tag_set(&tree, 0x100, RADIX_TREE_TAG_ALLOCATED);
    radix_tree_tag_set(&tree, 0x102, RADIX_TREE_TAG_ALLOCATED);
    
    // Add entries in different subtree
    radix_tree_insert(&tree, 0x200, (void *)0x2000);
    radix_tree_tag_set(&tree, 0x200, RADIX_TREE_TAG_ALLOCATED);
    
    // Clear tag from 0x100, parent should still have tag due to 0x102
    radix_tree_tag_clear(&tree, 0x100, RADIX_TREE_TAG_ALLOCATED);
    
    // Verify 0x102 still tagged
    if (!radix_tree_tag_get(&tree, 0x102, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag incorrectly cleared on sibling");
        return;
    }
    
    // Clear 0x102, now parent should not have tag
    radix_tree_tag_clear(&tree, 0x102, RADIX_TREE_TAG_ALLOCATED);
    
    // But 0x200 should still be tagged
    if (!radix_tree_tag_get(&tree, 0x200, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag incorrectly cleared on different subtree");
        return;
    }
    
    // Verify iteration only finds 0x200
    struct radix_tree_iter iter;
    void *entry = radix_tree_next_tagged(&tree, &iter, 0, RADIX_TREE_TAG_ALLOCATED);
    if (!entry || iter.index != 0x200) {
        TEST_FAIL("Wrong entry from tagged iteration");
        return;
    }
    
    entry = radix_tree_next_tagged(&tree, &iter, iter.next_index, RADIX_TREE_TAG_ALLOCATED);
    if (entry) {
        TEST_FAIL("Found extra tagged entry");
        return;
    }
    
    TEST_PASS();
}

static void test_max_height_operations(void) {
    TEST_START("Operations at maximum tree height");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    // Insert entries that require maximum height
    uint32_t keys[] = {
        0,
        0x40000000,  // Requires height 5
        0x80000000,  // Requires height 6
        0xC0000000,  // Still height 6
        0xFFFFFFFF   // Maximum possible key
    };
    int i;
    
    for (i = 0; i < 5; i++) {
        int ret = radix_tree_insert(&tree, keys[i], (void *)(unsigned long)(0x1000 + i));
        if (ret != 0) {
            TEST_FAIL("Failed to insert at max height");
            return;
        }
        radix_tree_tag_set(&tree, keys[i], RADIX_TREE_TAG_MSI);
    }
    
    // Verify tree is at maximum height
    struct radix_tree_stats stats;
    radix_tree_get_stats(&tree, &stats);
    if (stats.height != RADIX_TREE_MAX_HEIGHT) {
        TEST_FAIL("Tree not at maximum height");
        return;
    }
    
    // Test operations at max height
    for (i = 0; i < 5; i++) {
        void *val = radix_tree_lookup(&tree, keys[i]);
        if (val != (void *)(unsigned long)(0x1000 + i)) {
            TEST_FAIL("Lookup failed at max height");
            return;
        }
        
        if (!radix_tree_tag_get(&tree, keys[i], RADIX_TREE_TAG_MSI)) {
            TEST_FAIL("Tag not set at max height");
            return;
        }
    }
    
    // Test iteration at max height - limit iterations to prevent hang
    struct radix_tree_iter iter;
    uint32_t index = 0;
    int count = 0;
    int max_iterations = 10;  // Safety limit
    
    while (count < max_iterations) {
        void *entry = radix_tree_next_tagged(&tree, &iter, index, RADIX_TREE_TAG_MSI);
        if (!entry)
            break;
        count++;
        
        // Check for wraparound: if we found 0xFFFFFFFF, next_index will be 0
        // and we should stop iteration
        if (iter.index == 0xFFFFFFFF && iter.next_index == 0) {
            break; // End of iteration space
        }
        
        index = iter.next_index;
        
        // Safety check for infinite loop
        if (iter.next_index <= iter.index && iter.index != 0xFFFFFFFF && iter.index != 0) {
            TEST_FAIL("Iterator not advancing");
            return;
        }
    }
    
    if (count != 5) {
        uart_puts("    Expected 5 entries, found ");
        uart_putdec(count);
        uart_puts("\n");
        TEST_FAIL("Wrong iteration count at max height");
        return;
    }
    
    TEST_PASS();
}

static void test_replace_null_deletes(void) {
    TEST_START("Replace with NULL deletes entry");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    radix_tree_insert(&tree, 100, (void *)0x1000);
    radix_tree_tag_set(&tree, 100, RADIX_TREE_TAG_ALLOCATED);
    
    // Replace with NULL should delete
    void *old = radix_tree_replace(&tree, 100, NULL);
    if (old != (void *)0x1000) {
        TEST_FAIL("Replace with NULL returned wrong value");
        return;
    }
    
    // Entry should be gone
    void *val = radix_tree_lookup(&tree, 100);
    if (val != NULL) {
        TEST_FAIL("Entry still present after replace with NULL");
        return;
    }
    
    // Tag should be gone too
    if (radix_tree_tag_get(&tree, 100, RADIX_TREE_TAG_ALLOCATED)) {
        TEST_FAIL("Tag still set after replace with NULL");
        return;
    }
    
    TEST_PASS();
}

void test_radix_tree_edge(void) {
    uart_puts("\n=== Radix Tree Edge Case Tests ===\n");
    
    test_replace_preserves_tags();
    test_tree_shrink_with_tags();
    test_adjacent_keys_with_tags();
    test_power_of_2_boundaries();
    test_delete_during_iteration();
    test_zero_key_comprehensive();
    test_gang_lookup_sparse();
    test_tag_propagation_complex();
    test_max_height_operations();
    test_replace_null_deletes();
    
    uart_puts("\n=== Edge Test Summary ===\n");
    uart_puts("Passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nFailed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed > 0) {
        panic("Radix tree edge tests failed!");
    }
}