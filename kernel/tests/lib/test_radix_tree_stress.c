/*
 * kernel/tests/lib/test_radix_tree_stress.c
 *
 * Stress tests and bulk operations for radix tree
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

static uint32_t simple_rand(uint32_t *seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return *seed;
}

static void test_gang_lookup_empty(void) {
    TEST_START("Gang lookup on empty tree");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *results[10];
    
    unsigned int count = radix_tree_gang_lookup(&tree, results, 0, 10);
    if (count != 0) {
        TEST_FAIL("Gang lookup returned non-zero for empty tree");
        return;
    }
    
    TEST_PASS();
}

static void test_gang_lookup_sequential(void) {
    TEST_START("Gang lookup sequential");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *results[20];
    int i;
    
    for (i = 0; i < 30; i++) {
        radix_tree_insert(&tree, i, (void *)(unsigned long)(0x1000 + i));
    }
    
    unsigned int count = radix_tree_gang_lookup(&tree, results, 0, 20);
    if (count != 20) {
        TEST_FAIL("Wrong count from gang lookup");
        return;
    }
    
    for (i = 0; i < 20; i++) {
        void *expected = (void *)(unsigned long)(0x1000 + i);
        if (results[i] != expected) {
            TEST_FAIL("Wrong value from gang lookup");
            return;
        }
    }
    
    count = radix_tree_gang_lookup(&tree, results, 10, 10);
    if (count != 10) {
        TEST_FAIL("Wrong count from offset gang lookup");
        return;
    }
    
    for (i = 0; i < 10; i++) {
        void *expected = (void *)(unsigned long)(0x1000 + 10 + i);
        if (results[i] != expected) {
            TEST_FAIL("Wrong value from offset gang lookup");
            return;
        }
    }
    
    TEST_PASS();
}

static void test_gang_lookup_sparse(void) {
    TEST_START("Gang lookup sparse");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *results[10];
    uint32_t keys[] = {0, 100, 1000, 10000, 100000, 200000, 300000, 400000};
    int i;
    
    for (i = 0; i < 8; i++) {
        radix_tree_insert(&tree, keys[i], (void *)(unsigned long)(0x2000 + i));
    }
    
    unsigned int count = radix_tree_gang_lookup(&tree, results, 0, 10);
    if (count != 8) {
        TEST_FAIL("Wrong count from sparse gang lookup");
        return;
    }
    
    for (i = 0; i < 8; i++) {
        void *expected = (void *)(unsigned long)(0x2000 + i);
        if (results[i] != expected) {
            TEST_FAIL("Wrong value from sparse gang lookup");
            return;
        }
    }
    
    count = radix_tree_gang_lookup(&tree, results, 50000, 5);
    if (count != 4) {
        TEST_FAIL("Wrong count from middle sparse gang lookup");
        return;
    }
    
    TEST_PASS();
}

static void test_insert_1000_sequential(void) {
    TEST_START("Insert 1000 sequential");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    int i;
    
    for (i = 0; i < 1000; i++) {
        void *val = (void *)(unsigned long)(0x3000 + i);
        int ret = radix_tree_insert(&tree, i, val);
        if (ret != 0) {
            TEST_FAIL("Insert failed in sequential test");
            return;
        }
    }
    
    for (i = 0; i < 1000; i++) {
        void *expected = (void *)(unsigned long)(0x3000 + i);
        void *val = radix_tree_lookup(&tree, i);
        if (val != expected) {
            TEST_FAIL("Lookup failed in sequential test");
            return;
        }
    }
    
    struct radix_tree_stats stats;
    radix_tree_get_stats(&tree, &stats);
    
    if (stats.entries != 1000) {
        TEST_FAIL("Wrong entry count after 1000 inserts");
        return;
    }
    
    TEST_PASS();
}

static void test_insert_1000_random(void) {
    TEST_START("Insert 1000 random");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t keys[1000];
    uint32_t seed = 12345;
    int i;
    
    for (i = 0; i < 1000; i++) {
        keys[i] = simple_rand(&seed) % 1000000;
        void *val = (void *)(unsigned long)(0x4000 + i);
        
        if (radix_tree_lookup(&tree, keys[i]) == NULL) {
            radix_tree_insert(&tree, keys[i], val);
        } else {
            keys[i] = 0xFFFFFFFF;
        }
    }
    
    for (i = 0; i < 1000; i++) {
        if (keys[i] != 0xFFFFFFFF) {
            void *expected = (void *)(unsigned long)(0x4000 + i);
            void *val = radix_tree_lookup(&tree, keys[i]);
            if (val != expected) {
                TEST_FAIL("Lookup failed in random test");
                return;
            }
        }
    }
    
    TEST_PASS();
}

static void test_delete_half(void) {
    TEST_START("Delete half of entries");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    int i;
    
    for (i = 0; i < 100; i++) {
        radix_tree_insert(&tree, i, (void *)(unsigned long)(0x5000 + i));
    }
    
    for (i = 0; i < 100; i += 2) {
        void *val = radix_tree_delete(&tree, i);
        if (!val) {
            TEST_FAIL("Delete failed");
            return;
        }
    }
    
    for (i = 0; i < 100; i++) {
        void *val = radix_tree_lookup(&tree, i);
        if (i % 2 == 0) {
            if (val != NULL) {
                TEST_FAIL("Deleted entry still present");
                return;
            }
        } else {
            void *expected = (void *)(unsigned long)(0x5000 + i);
            if (val != expected) {
                TEST_FAIL("Non-deleted entry wrong");
                return;
            }
        }
    }
    
    TEST_PASS();
}

static void test_churn(void) {
    TEST_START("Insert/delete churn");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t seed = 54321;
    int i, j;
    
    for (j = 0; j < 10; j++) {
        for (i = 0; i < 100; i++) {
            uint32_t key = simple_rand(&seed) % 1000;
            void *val = (void *)(unsigned long)(0x6000 + key);
            
            if (radix_tree_lookup(&tree, key)) {
                radix_tree_delete(&tree, key);
            } else {
                radix_tree_insert(&tree, key, val);
            }
        }
    }
    
    struct radix_tree_iter iter;
    void *entry;
    uint32_t index = 0;
    int count = 0;
    
    while ((entry = radix_tree_next_slot(&tree, &iter, index))) {
        count++;
        index = iter.next_index;
    }
    
    if (count == 0) {
        TEST_FAIL("No entries after churn");
        return;
    }
    
    TEST_PASS();
}

static void test_msi_pattern(void) {
    TEST_START("MSI allocation pattern");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t base_irqs[] = {0x1000, 0x2000, 0x3000, 0x10000, 0x20000};
    int i, j;
    
    for (i = 0; i < 5; i++) {
        uint32_t base = base_irqs[i];
        for (j = 0; j < 32; j++) {
            void *val = (void *)(unsigned long)(0x7000 + i * 32 + j);
            int ret = radix_tree_insert(&tree, base + j, val);
            if (ret != 0) {
                TEST_FAIL("MSI insert failed");
                return;
            }
            radix_tree_tag_set(&tree, base + j, RADIX_TREE_TAG_MSI);
        }
    }
    
    for (i = 0; i < 5; i++) {
        uint32_t base = base_irqs[i];
        void *results[32];
        unsigned int count = radix_tree_gang_lookup(&tree, results, base, 32);
        
        if (count != 32) {
            TEST_FAIL("Wrong MSI gang lookup count");
            return;
        }
        
        for (j = 0; j < 32; j++) {
            void *expected = (void *)(unsigned long)(0x7000 + i * 32 + j);
            if (results[j] != expected) {
                TEST_FAIL("Wrong MSI value");
                return;
            }
            
            if (!radix_tree_tag_get(&tree, base + j, RADIX_TREE_TAG_MSI)) {
                TEST_FAIL("MSI tag not set");
                return;
            }
        }
    }
    
    TEST_PASS();
}

static void test_very_sparse(void) {
    TEST_START("Very sparse allocations");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    uint32_t keys[] = {
        0,
        0xFF,
        0xFFFF,
        0xFFFFFF,
        0x80000000,
        0xC0000000,
        0xFFFFFFF0
    };
    int i;
    
    for (i = 0; i < 7; i++) {
        void *val = (void *)(unsigned long)(0x8000 + i);
        int ret = radix_tree_insert(&tree, keys[i], val);
        if (ret != 0) {
            TEST_FAIL("Very sparse insert failed");
            return;
        }
    }
    
    for (i = 0; i < 7; i++) {
        void *expected = (void *)(unsigned long)(0x8000 + i);
        void *val = radix_tree_lookup(&tree, keys[i]);
        if (val != expected) {
            TEST_FAIL("Very sparse lookup failed");
            return;
        }
    }
    
    struct radix_tree_stats stats;
    radix_tree_get_stats(&tree, &stats);
    
    if (stats.entries != 7) {
        TEST_FAIL("Wrong entry count for very sparse");
        return;
    }
    
    TEST_PASS();
}

static void test_maximum_height(void) {
    TEST_START("Maximum tree height");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    
    radix_tree_insert(&tree, 0, (void *)0x9000);
    radix_tree_insert(&tree, 0xFFFFFFFF, (void *)0x9001);
    
    struct radix_tree_stats stats;
    radix_tree_get_stats(&tree, &stats);
    
    if (stats.height != RADIX_TREE_MAX_HEIGHT) {
        TEST_FAIL("Tree not at maximum height");
        return;
    }
    
    void *val = radix_tree_lookup(&tree, 0);
    if (val != (void *)0x9000) {
        TEST_FAIL("Lookup at 0 failed");
        return;
    }
    
    val = radix_tree_lookup(&tree, 0xFFFFFFFF);
    if (val != (void *)0x9001) {
        TEST_FAIL("Lookup at max failed");
        return;
    }
    
    TEST_PASS();
}

static void test_gang_lookup_boundary(void) {
    TEST_START("Gang lookup boundary conditions");
    
    struct radix_tree_root tree = RADIX_TREE_INIT;
    void *results[10];
    
    unsigned int count = radix_tree_gang_lookup(&tree, NULL, 0, 10);
    if (count != 0) {
        TEST_FAIL("Gang lookup with NULL results");
        return;
    }
    
    count = radix_tree_gang_lookup(&tree, results, 0, 0);
    if (count != 0) {
        TEST_FAIL("Gang lookup with 0 max_items");
        return;
    }
    
    radix_tree_insert(&tree, 5, (void *)0xA000);
    
    count = radix_tree_gang_lookup(&tree, results, 10, 10);
    if (count != 0) {
        TEST_FAIL("Gang lookup past last entry");
        return;
    }
    
    count = radix_tree_gang_lookup(&tree, results, 5, 10);
    if (count != 1) {
        TEST_FAIL("Gang lookup at exact entry");
        return;
    }
    
    TEST_PASS();
}

void test_radix_tree_stress(void) {
    uart_puts("\n=== Radix Tree Stress Tests ===\n");
    
    test_gang_lookup_empty();
    test_gang_lookup_sequential();
    test_gang_lookup_sparse();
    test_insert_1000_sequential();
    test_insert_1000_random();
    test_delete_half();
    test_churn();
    test_msi_pattern();
    test_very_sparse();
    test_maximum_height();
    test_gang_lookup_boundary();
    
    uart_puts("\n=== Stress Test Summary ===\n");
    uart_puts("Passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nFailed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed > 0) {
        panic("Radix tree stress tests failed!");
    }
}