/*
 * kernel/tests/device_tests.c
 * 
 * Device management test suite implementation
 */

#include <tests/device_tests.h>
#include <device/device.h>
#include <device/resource.h>
#include <device/device_tree.h>
#include <uart.h>
#include <string.h>

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

/* Helper to run a test */
#define RUN_TEST(test_name) do { \
    uart_puts("  - " #test_name "... "); \
    tests_run++; \
    if (test_##test_name()) { \
        uart_puts("PASS\n"); \
        tests_passed++; \
    } else { \
        uart_puts("FAIL\n"); \
    } \
} while(0)

/* Test device allocation and registration */
static bool test_device_registration(void) {
    struct device *dev;
    
    /* Test device allocation */
    dev = device_alloc();
    if (!dev) {
        return false;
    }
    
    /* Check initial state */
    if (dev->id == 0 || dev->type != DEV_TYPE_UNKNOWN) {
        return false;
    }
    
    /* Test device registration */
    struct device *reg_dev = device_register("test-device", DEV_TYPE_PLATFORM);
    if (!reg_dev) {
        return false;
    }
    
    /* Verify registration */
    if (strcmp(reg_dev->name, "test-device") != 0 ||
        reg_dev->type != DEV_TYPE_PLATFORM) {
        return false;
    }
    
    return true;
}

/* Test device lookup functions */
static bool test_device_lookup(void) {
    struct device *dev1, *dev2, *found;
    
    /* Register test devices */
    dev1 = device_register("lookup-test-1", DEV_TYPE_UART);
    dev2 = device_register("lookup-test-2", DEV_TYPE_TIMER);
    
    if (!dev1 || !dev2) {
        return false;
    }
    
    /* Test find by name */
    found = device_find_by_name("lookup-test-1");
    if (found != dev1) {
        return false;
    }
    
    /* Test find by type */
    found = device_find_by_type(DEV_TYPE_TIMER);
    if (found != dev2) {
        return false;
    }
    
    /* Test find by ID */
    found = device_find_by_id(dev1->id);
    if (found != dev1) {
        return false;
    }
    
    /* Test not found */
    found = device_find_by_name("non-existent");
    if (found != NULL) {
        return false;
    }
    
    return true;
}

/* Test device tree hierarchy */
static bool test_device_hierarchy(void) {
    struct device *parent, *child1, *child2, *found;
    
    /* Create parent device */
    parent = device_register("parent-device", DEV_TYPE_PLATFORM);
    if (!parent) {
        return false;
    }
    
    /* Create child devices */
    child1 = device_register("child-1", DEV_TYPE_GPIO);
    child2 = device_register("child-2", DEV_TYPE_I2C);
    if (!child1 || !child2) {
        return false;
    }
    
    /* Add children to parent */
    device_add_child(parent, child1);
    device_add_child(parent, child2);
    
    /* Verify relationships */
    if (child1->parent != parent || child2->parent != parent) {
        return false;
    }
    
    if (parent->children != child2) {  /* Last added is first in list */
        return false;
    }
    
    /* Test child lookup */
    found = device_get_child(parent, "child-1");
    if (found != child1) {
        return false;
    }
    
    /* Test child removal */
    device_remove_child(parent, child1);
    if (child1->parent != NULL || parent->children != child2) {
        return false;
    }
    
    return true;
}

/* Test resource management */
static bool test_resource_management(void) {
    struct device *dev;
    struct resource *res;
    int ret;
    
    /* Create test device */
    dev = device_register("resource-test", DEV_TYPE_PLATFORM);
    if (!dev) {
        return false;
    }
    
    /* Add memory resource */
    ret = device_add_mem_resource(dev, 0x10000000, 0x1000, 
                                 RES_MEM_CACHEABLE, "test-mem");
    if (ret < 0) {
        return false;
    }
    
    /* Add IRQ resource */
    ret = device_add_irq_resource(dev, 42, RES_IRQ_LEVEL | RES_IRQ_SHARED,
                                 "test-irq");
    if (ret < 0) {
        return false;
    }
    
    /* Verify resource count */
    if (dev->num_resources != 2) {
        return false;
    }
    
    /* Test resource lookup by type and index */
    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res || res->start != 0x10000000 || resource_size(res) != 0x1000) {
        return false;
    }
    
    /* Test resource lookup by name */
    res = device_get_resource_by_name(dev, RES_TYPE_IRQ, "test-irq");
    if (!res || res->start != 42) {
        return false;
    }
    
    return true;
}

/* Test resource validation */
static bool test_resource_validation(void) {
    struct resource res1, res2;
    
    /* Initialize resources */
    resource_init(&res1, RES_TYPE_MEM, 0x1000, 0x1FFF, "res1");
    resource_init(&res2, RES_TYPE_MEM, 0x1800, 0x2800, "res2");
    
    /* Test resource size */
    if (resource_size(&res1) != 0x1000) {
        return false;
    }
    
    /* Test overlap detection */
    if (!resource_overlaps(&res1, &res2)) {
        return false;
    }
    
    /* Test containment */
    struct resource parent_res, child_res;
    resource_init(&parent_res, RES_TYPE_MEM, 0x0, 0xFFFF, "parent");
    resource_init(&child_res, RES_TYPE_MEM, 0x1000, 0x1FFF, "child");
    
    if (!resource_contains(&parent_res, &child_res)) {
        return false;
    }
    
    return true;
}

/* Test device type conversions */
static bool test_device_type_conversion(void) {
    const char *type_str;
    device_type_t type;
    
    /* Test type to string */
    type_str = device_type_to_string(DEV_TYPE_UART);
    if (strcmp(type_str, "uart") != 0) {
        return false;
    }
    
    /* Test string to type */
    type = device_string_to_type("timer");
    if (type != DEV_TYPE_TIMER) {
        return false;
    }
    
    /* Test invalid type */
    type_str = device_type_to_string((device_type_t)999);
    if (strcmp(type_str, "invalid") != 0) {
        return false;
    }
    
    return true;
}

/* Test early pool functionality */
static bool test_early_pool(void) {
    extern bool early_pool_check_integrity(void);
    extern size_t early_pool_get_free(void);
    
    /* Check pool integrity */
    if (!early_pool_check_integrity()) {
        return false;
    }
    
    /* Verify we have free space */
    if (early_pool_get_free() == 0) {
        return false;
    }
    
    return true;
}

/* Main test runner */
void run_device_tests(void) {
    uart_puts("\n========================================\n");
    uart_puts("Running Device Management Tests\n");
    uart_puts("========================================\n\n");
    
    RUN_TEST(device_registration);
    RUN_TEST(device_lookup);
    RUN_TEST(device_hierarchy);
    RUN_TEST(resource_management);
    RUN_TEST(resource_validation);
    RUN_TEST(device_type_conversion);
    RUN_TEST(early_pool);
    
    uart_puts("\n----------------------------------------\n");
    uart_puts("Test Summary: ");
    uart_puthex(tests_passed);
    uart_puts("/");
    uart_puthex(tests_run);
    uart_puts(" tests passed\n");
    
    if (tests_passed == tests_run) {
        uart_puts("All tests PASSED!\n");
    } else {
        uart_puts("Some tests FAILED!\n");
    }
    uart_puts("========================================\n\n");
}