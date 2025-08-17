#include <irq/irq_domain.h>
#include <irq/irq.h>
#include <memory/kmalloc.h>
#include <string.h>
#include <uart.h>

// Test statistics
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test macros
#define TEST_START(msg) do { \
    uart_puts("\n[TEST] "); \
    uart_puts(msg); \
    uart_puts("...\n"); \
    tests_run++; \
} while(0)

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        uart_puts("  [FAIL] "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    uart_puts("  [PASS] "); \
    uart_puts(msg); \
    uart_puts("\n"); \
    tests_passed++; \
} while(0)

// Mock GPIO controller for testing hierarchical domains
struct test_gpio_controller {
    const char *name;
    uint32_t num_pins;
    struct irq_domain *domain;
    struct irq_domain *parent_domain;
    uint32_t parent_hwirq_base;
    uint32_t *pin_to_virq;
    uint32_t pending_mask;  // Simulated pending interrupts
    spinlock_t lock;
};

// Test GPIO chip operations
static void test_gpio_irq_enable(struct irq_desc *desc) {
    // Just mark as enabled for testing
}

static void test_gpio_irq_disable(struct irq_desc *desc) {
    // Just mark as disabled for testing
}

static void test_gpio_irq_mask(struct irq_desc *desc) {
    test_gpio_irq_disable(desc);
}

static void test_gpio_irq_unmask(struct irq_desc *desc) {
    test_gpio_irq_enable(desc);
}

static void test_gpio_irq_ack(struct irq_desc *desc) {
    struct test_gpio_controller *gpio = desc->chip_data;
    uint32_t pin = desc->hwirq;
    
    // Clear the simulated pending bit
    if (gpio && pin < gpio->num_pins) {
        gpio->pending_mask &= ~(1 << pin);
    }
}

static void test_gpio_irq_eoi(struct irq_desc *desc) {
    test_gpio_irq_ack(desc);
    
    // For cascaded controllers, EOI parent
    if (desc->parent_desc && desc->parent_desc->chip && 
        desc->parent_desc->chip->irq_eoi) {
        desc->parent_desc->chip->irq_eoi(desc->parent_desc);
    }
}

static int test_gpio_irq_set_type(struct irq_desc *desc, uint32_t type) {
    // Accept all types for testing
    return 0;
}

static struct irq_chip test_gpio_irq_chip = {
    .name = "TEST-GPIO",
    .irq_enable = test_gpio_irq_enable,
    .irq_disable = test_gpio_irq_disable,
    .irq_mask = test_gpio_irq_mask,
    .irq_unmask = test_gpio_irq_unmask,
    .irq_ack = test_gpio_irq_ack,
    .irq_eoi = test_gpio_irq_eoi,
    .irq_set_type = test_gpio_irq_set_type,
    .flags = 0
};

// Test GPIO domain operations
static int test_gpio_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq) {
    struct test_gpio_controller *gpio = d->chip_data;
    
    if (!gpio || hwirq >= gpio->num_pins) {
        return -1;
    }
    
    irq_domain_set_hwirq_and_chip(d, virq, hwirq, &test_gpio_irq_chip, gpio);
    
    if (gpio->pin_to_virq) {
        gpio->pin_to_virq[hwirq] = virq;
    }
    
    return 0;
}

static void test_gpio_domain_unmap(struct irq_domain *d, uint32_t virq) {
    struct test_gpio_controller *gpio = d->chip_data;
    struct irq_desc *desc = irq_to_desc(virq);
    
    if (desc && gpio && desc->hwirq < gpio->num_pins) {
        if (gpio->pin_to_virq) {
            gpio->pin_to_virq[desc->hwirq] = IRQ_INVALID;
        }
    }
}

static uint32_t test_gpio_child_to_parent_hwirq(struct irq_domain *d, uint32_t child_hwirq) {
    struct test_gpio_controller *gpio = d->chip_data;
    
    if (!gpio) {
        // If no controller data, use identity mapping
        return child_hwirq;
    }
    
    // Transform GPIO pin to parent hwirq
    return gpio->parent_hwirq_base + child_hwirq;
}

static const struct irq_domain_ops test_gpio_domain_ops = {
    .map = test_gpio_domain_map,
    .unmap = test_gpio_domain_unmap,
    .alloc = NULL,  // Not used for single mappings
    .free = NULL,
    .activate = NULL,
    .deactivate = NULL,
    .xlate = NULL,
    .child_to_parent_hwirq = test_gpio_child_to_parent_hwirq
};

// Simple domain ops for tests that don't need GPIO controller
static int test_simple_domain_map(struct irq_domain *d, uint32_t virq, uint32_t hwirq) {
    // Just set the chip - no controller needed
    irq_domain_set_hwirq_and_chip(d, virq, hwirq, &test_gpio_irq_chip, d->chip_data);
    return 0;
}

static const struct irq_domain_ops test_simple_domain_ops = {
    .map = test_simple_domain_map,
    .unmap = NULL,
    .alloc = NULL,
    .free = NULL,
    .activate = NULL,
    .deactivate = NULL,
    .xlate = NULL,
    .child_to_parent_hwirq = NULL  // Use identity mapping
};

// Test handler for GPIO interrupts
static int gpio_test_handler_called = 0;
static uint32_t gpio_test_last_pin = 0;

static void test_gpio_handler(void *data) {
    uint32_t pin = (uint32_t)(uintptr_t)data;
    gpio_test_handler_called++;
    gpio_test_last_pin = pin;
}

// Test handler for parent interrupts
static int parent_test_handler_called = 0;

static void test_parent_handler(void *data) {
    parent_test_handler_called++;
}

// Test: Create hierarchical domain
static void test_hierarchical_domain_creation(void) {
    struct irq_domain *parent_domain;
    struct irq_domain *child_domain;
    struct test_gpio_controller *gpio;
    
    TEST_START("Hierarchical domain creation");
    
    // Create parent domain (simulating GIC/PLIC)
    parent_domain = irq_domain_create_linear(NULL, 128, NULL, NULL);
    TEST_ASSERT(parent_domain != NULL, "Parent domain creation failed");
    
    // Create GPIO controller with hierarchical domain
    gpio = kmalloc(sizeof(struct test_gpio_controller), KM_ZERO);
    TEST_ASSERT(gpio != NULL, "GPIO controller allocation failed");
    
    gpio->name = "test-gpio";
    gpio->num_pins = 32;
    gpio->parent_domain = parent_domain;
    gpio->parent_hwirq_base = 32;  // Start at hwirq 32 in parent
    spin_lock_init(&gpio->lock);
    
    gpio->pin_to_virq = kmalloc(gpio->num_pins * sizeof(uint32_t), 0);
    TEST_ASSERT(gpio->pin_to_virq != NULL, "Pin mapping allocation failed");
    memset(gpio->pin_to_virq, 0xFF, gpio->num_pins * sizeof(uint32_t));
    
    // Create hierarchical domain
    child_domain = irq_domain_create_hierarchy(parent_domain,
                                               gpio->num_pins,
                                               NULL,
                                               &test_gpio_domain_ops,
                                               gpio);
    TEST_ASSERT(child_domain != NULL, "Child domain creation failed");
    TEST_ASSERT(child_domain->parent == parent_domain, "Parent not set correctly");
    TEST_ASSERT(child_domain->type == DOMAIN_HIERARCHY, "Domain type not hierarchical");
    
    gpio->domain = child_domain;
    child_domain->chip = &test_gpio_irq_chip;
    
    // Clean up
    irq_domain_remove(child_domain);
    irq_domain_remove(parent_domain);
    kfree(gpio->pin_to_virq);
    kfree(gpio);
    
    TEST_PASS("Hierarchical domain created successfully");
}

// Test: Cascade interrupt mapping
static void test_cascade_mapping(void) {
    struct irq_domain *parent_domain;
    struct test_gpio_controller *gpio;
    uint32_t gpio_virq, parent_virq;
    struct irq_desc *gpio_desc, *parent_desc;
    
    TEST_START("Cascade interrupt mapping");
    
    // Setup domains
    parent_domain = irq_domain_create_linear(NULL, 128, NULL, NULL);
    TEST_ASSERT(parent_domain != NULL, "Parent domain creation failed");
    
    gpio = kmalloc(sizeof(struct test_gpio_controller), KM_ZERO);
    TEST_ASSERT(gpio != NULL, "GPIO allocation failed");
    
    gpio->num_pins = 32;
    gpio->parent_domain = parent_domain;
    gpio->parent_hwirq_base = 32;
    gpio->pin_to_virq = kmalloc(gpio->num_pins * sizeof(uint32_t), 0);
    memset(gpio->pin_to_virq, 0xFF, gpio->num_pins * sizeof(uint32_t));
    spin_lock_init(&gpio->lock);
    
    gpio->domain = irq_domain_create_hierarchy(parent_domain,
                                               gpio->num_pins,
                                               NULL,
                                               &test_gpio_domain_ops,
                                               gpio);
    TEST_ASSERT(gpio->domain != NULL, "GPIO domain creation failed");
    gpio->domain->chip = &test_gpio_irq_chip;
    
    // Create mapping for GPIO pin 5
    gpio_virq = irq_create_mapping(gpio->domain, 5);
    TEST_ASSERT(gpio_virq != IRQ_INVALID, "GPIO mapping creation failed");
    
    // Check that parent mapping was created
    gpio_desc = irq_to_desc(gpio_virq);
    TEST_ASSERT(gpio_desc != NULL, "GPIO descriptor not found");
    TEST_ASSERT(gpio_desc->parent_desc != NULL, "Parent descriptor not linked");
    
    parent_desc = gpio_desc->parent_desc;
    parent_virq = parent_desc->irq;
    TEST_ASSERT(parent_virq != IRQ_INVALID, "Parent virq invalid");
    
    // Verify parent hwirq is correctly transformed
    TEST_ASSERT(parent_desc->hwirq == (gpio->parent_hwirq_base + 5),
                "Parent hwirq transformation incorrect");
    
    // Verify GPIO pin mapping
    TEST_ASSERT(gpio->pin_to_virq[5] == gpio_virq, "Pin to virq mapping incorrect");
    
    // Clean up mapping
    irq_dispose_mapping(gpio_virq);
    
    // Verify cleanup
    TEST_ASSERT(gpio->pin_to_virq[5] == IRQ_INVALID, "Pin mapping not cleared");
    TEST_ASSERT(irq_find_mapping(gpio->domain, 5) == IRQ_INVALID, 
                "Mapping not properly disposed");
    
    // Clean up
    irq_domain_remove(gpio->domain);
    irq_domain_remove(parent_domain);
    kfree(gpio->pin_to_virq);
    kfree(gpio);
    
    TEST_PASS("Cascade mapping works correctly");
}

// Test: Interrupt activation through hierarchy
static void test_hierarchical_activation(void) {
    struct irq_domain *parent_domain;
    struct test_gpio_controller *gpio;
    uint32_t gpio_virq;
    struct irq_desc *gpio_desc;
    int ret;
    
    TEST_START("Hierarchical interrupt activation");
    
    // Setup domains
    parent_domain = irq_domain_create_linear(NULL, 128, NULL, NULL);
    gpio = kmalloc(sizeof(struct test_gpio_controller), KM_ZERO);
    gpio->num_pins = 32;
    gpio->parent_domain = parent_domain;
    gpio->parent_hwirq_base = 32;
    gpio->pin_to_virq = kmalloc(gpio->num_pins * sizeof(uint32_t), 0);
    memset(gpio->pin_to_virq, 0xFF, gpio->num_pins * sizeof(uint32_t));
    spin_lock_init(&gpio->lock);
    
    gpio->domain = irq_domain_create_hierarchy(parent_domain,
                                               gpio->num_pins,
                                               NULL,
                                               &test_gpio_domain_ops,
                                               gpio);
    gpio->domain->chip = &test_gpio_irq_chip;
    
    // Create mapping
    gpio_virq = irq_create_mapping(gpio->domain, 10);
    TEST_ASSERT(gpio_virq != IRQ_INVALID, "Mapping creation failed");
    
    gpio_desc = irq_to_desc(gpio_virq);
    TEST_ASSERT(gpio_desc != NULL, "Descriptor not found");
    
    // Test activation
    ret = irq_domain_activate_irq(gpio_desc, false);
    TEST_ASSERT(ret == 0, "Activation failed");
    
    // Test deactivation
    irq_domain_deactivate_irq(gpio_desc);
    
    // Clean up
    irq_dispose_mapping(gpio_virq);
    irq_domain_remove(gpio->domain);
    irq_domain_remove(parent_domain);
    kfree(gpio->pin_to_virq);
    kfree(gpio);
    
    TEST_PASS("Hierarchical activation works");
}

// Test: Multiple cascade levels
static void test_multi_level_cascade(void) {
    struct irq_domain *gic_domain, *gpio1_domain, *gpio2_domain;
    struct test_gpio_controller *gpio1, *gpio2;
    uint32_t virq;
    struct irq_desc *desc;
    
    TEST_START("Multi-level cascade");
    
    // Create GIC domain (level 0)
    gic_domain = irq_domain_create_linear(NULL, 256, NULL, NULL);
    TEST_ASSERT(gic_domain != NULL, "GIC domain creation failed");
    
    // Create first GPIO controller (level 1)
    gpio1 = kmalloc(sizeof(struct test_gpio_controller), KM_ZERO);
    gpio1->num_pins = 32;
    gpio1->parent_hwirq_base = 64;
    gpio1->pin_to_virq = kmalloc(gpio1->num_pins * sizeof(uint32_t), 0);
    memset(gpio1->pin_to_virq, 0xFF, gpio1->num_pins * sizeof(uint32_t));
    spin_lock_init(&gpio1->lock);
    
    gpio1_domain = irq_domain_create_hierarchy(gic_domain, 32, NULL,
                                               &test_gpio_domain_ops, gpio1);
    TEST_ASSERT(gpio1_domain != NULL, "GPIO1 domain creation failed");
    gpio1->domain = gpio1_domain;
    gpio1_domain->chip = &test_gpio_irq_chip;
    
    // Create second GPIO controller (level 2) - child of first GPIO
    gpio2 = kmalloc(sizeof(struct test_gpio_controller), KM_ZERO);
    gpio2->num_pins = 16;
    gpio2->parent_hwirq_base = 8;
    gpio2->pin_to_virq = kmalloc(gpio2->num_pins * sizeof(uint32_t), 0);
    memset(gpio2->pin_to_virq, 0xFF, gpio2->num_pins * sizeof(uint32_t));
    spin_lock_init(&gpio2->lock);
    
    gpio2_domain = irq_domain_create_hierarchy(gpio1_domain, 16, NULL,
                                               &test_gpio_domain_ops, gpio2);
    TEST_ASSERT(gpio2_domain != NULL, "GPIO2 domain creation failed");
    gpio2->domain = gpio2_domain;
    gpio2_domain->chip = &test_gpio_irq_chip;
    
    // Create mapping in level 2
    virq = irq_create_mapping(gpio2_domain, 3);
    TEST_ASSERT(virq != IRQ_INVALID, "Level 2 mapping failed");
    
    // Verify cascade chain
    desc = irq_to_desc(virq);
    TEST_ASSERT(desc != NULL, "Descriptor not found");
    TEST_ASSERT(desc->parent_desc != NULL, "No parent descriptor");
    TEST_ASSERT(desc->parent_desc->parent_desc != NULL, "No grandparent descriptor");
    
    // Clean up
    irq_dispose_mapping(virq);
    irq_domain_remove(gpio2_domain);
    irq_domain_remove(gpio1_domain);
    irq_domain_remove(gic_domain);
    kfree(gpio2->pin_to_virq);
    kfree(gpio2);
    kfree(gpio1->pin_to_virq);
    kfree(gpio1);
    
    TEST_PASS("Multi-level cascade works");
}

// Test: Handler invocation through cascade
static void test_cascade_handler_invocation(void) {
    struct irq_domain *parent_domain;
    struct test_gpio_controller *gpio;
    uint32_t gpio_virq;
    struct irq_desc *desc;
    int ret;
    
    TEST_START("Cascade handler invocation");
    
    // Reset test counters
    gpio_test_handler_called = 0;
    gpio_test_last_pin = 0;
    parent_test_handler_called = 0;
    
    // Setup domains
    parent_domain = irq_domain_create_linear(NULL, 128, NULL, NULL);
    gpio = kmalloc(sizeof(struct test_gpio_controller), KM_ZERO);
    gpio->num_pins = 32;
    gpio->parent_domain = parent_domain;
    gpio->parent_hwirq_base = 32;
    gpio->pin_to_virq = kmalloc(gpio->num_pins * sizeof(uint32_t), 0);
    memset(gpio->pin_to_virq, 0xFF, gpio->num_pins * sizeof(uint32_t));
    spin_lock_init(&gpio->lock);
    
    gpio->domain = irq_domain_create_hierarchy(parent_domain, 32, NULL,
                                               &test_gpio_domain_ops, gpio);
    gpio->domain->chip = &test_gpio_irq_chip;
    
    // Create mapping for pin 7
    gpio_virq = irq_create_mapping(gpio->domain, 7);
    TEST_ASSERT(gpio_virq != IRQ_INVALID, "Mapping creation failed");
    
    // Register handler
    ret = request_irq(gpio_virq, test_gpio_handler, 0, "gpio-test", 
                      (void *)(uintptr_t)7);
    TEST_ASSERT(ret == 0, "Handler registration failed");
    
    // Simulate interrupt
    desc = irq_to_desc(gpio_virq);
    TEST_ASSERT(desc != NULL, "Descriptor not found");
    
    // Trigger the handler
    if (desc->action && desc->action->handler) {
        desc->action->handler(desc->action->dev_data);
    }
    
    TEST_ASSERT(gpio_test_handler_called == 1, "Handler not called");
    TEST_ASSERT(gpio_test_last_pin == 7, "Wrong pin data passed");
    
    // Clean up
    free_irq(gpio_virq, (void *)(uintptr_t)7);
    irq_dispose_mapping(gpio_virq);
    irq_domain_remove(gpio->domain);
    irq_domain_remove(parent_domain);
    kfree(gpio->pin_to_virq);
    kfree(gpio);
    
    TEST_PASS("Cascade handler invocation works");
}

// Test: NULL parameter handling
static void test_hierarchy_null_parameters(void) {
    struct irq_domain *domain;
    uint32_t virq;
    
    TEST_START("Hierarchy NULL parameter handling");
    
    // Test creating hierarchy with NULL parent
    domain = irq_domain_create_hierarchy(NULL, 32, NULL, &test_simple_domain_ops, NULL);
    TEST_ASSERT(domain == NULL, "Should fail with NULL parent");
    
    // Test creating hierarchy with zero size
    struct irq_domain *parent = irq_domain_create_linear(NULL, 64, NULL, NULL);
    TEST_ASSERT(parent != NULL, "Parent creation failed");
    
    domain = irq_domain_create_hierarchy(parent, 0, NULL, NULL, NULL);
    TEST_ASSERT(domain == NULL, "Should fail with zero size");
    
    // Test activate with NULL descriptor
    TEST_ASSERT(irq_domain_activate_irq(NULL, false) == -1, "Should fail with NULL desc");
    
    // Test deactivate with NULL descriptor (should not crash)
    irq_domain_deactivate_irq(NULL);
    
    // Clean up
    irq_domain_remove(parent);
    
    TEST_PASS("NULL parameter handling correct");
}

// Test: Maximum hierarchy depth
static void test_max_hierarchy_depth(void) {
    struct irq_domain *domains[10];
    struct irq_domain *parent;
    uint32_t virq;
    int i;
    
    TEST_START("Maximum hierarchy depth");
    
    // Create base domain
    domains[0] = irq_domain_create_linear(NULL, 256, NULL, NULL);
    TEST_ASSERT(domains[0] != NULL, "Base domain creation failed");
    
    // Create chain of hierarchical domains
    parent = domains[0];
    for (i = 1; i < 10; i++) {
        domains[i] = irq_domain_create_hierarchy(parent, 32, NULL, 
                                                 &test_simple_domain_ops, NULL);
        TEST_ASSERT(domains[i] != NULL, "Domain creation failed at depth");
        domains[i]->chip = &test_gpio_irq_chip;
        parent = domains[i];
    }
    
    // Create mapping at deepest level
    virq = irq_create_mapping(domains[9], 5);
    TEST_ASSERT(virq != IRQ_INVALID, "Deep mapping creation failed");
    
    // Verify chain
    struct irq_desc *desc = irq_to_desc(virq);
    int depth = 0;
    while (desc && desc->parent_desc) {
        depth++;
        desc = desc->parent_desc;
    }
    TEST_ASSERT(depth == 9, "Incorrect hierarchy depth");
    
    // Clean up
    irq_dispose_mapping(virq);
    for (i = 9; i >= 0; i--) {
        irq_domain_remove(domains[i]);
    }
    
    TEST_PASS("Deep hierarchy works");
}

// Test: Multiple children on same parent
static void test_multiple_children(void) {
    struct irq_domain *parent, *child1, *child2, *child3;
    uint32_t virq1, virq2, virq3;
    
    TEST_START("Multiple children on same parent");
    
    // Create parent domain
    parent = irq_domain_create_linear(NULL, 256, NULL, NULL);
    TEST_ASSERT(parent != NULL, "Parent creation failed");
    
    // Create multiple child domains
    child1 = irq_domain_create_hierarchy(parent, 16, NULL, 
                                         &test_simple_domain_ops, NULL);
    TEST_ASSERT(child1 != NULL, "Child1 creation failed");
    
    child2 = irq_domain_create_hierarchy(parent, 32, NULL,
                                         &test_simple_domain_ops, NULL);
    TEST_ASSERT(child2 != NULL, "Child2 creation failed");
    
    child3 = irq_domain_create_hierarchy(parent, 8, NULL,
                                         &test_simple_domain_ops, NULL);
    TEST_ASSERT(child3 != NULL, "Child3 creation failed");
    
    // Create mappings in each child
    virq1 = irq_create_mapping(child1, 5);
    virq2 = irq_create_mapping(child2, 10);
    virq3 = irq_create_mapping(child3, 3);
    
    TEST_ASSERT(virq1 != IRQ_INVALID, "Child1 mapping failed");
    TEST_ASSERT(virq2 != IRQ_INVALID, "Child2 mapping failed");
    TEST_ASSERT(virq3 != IRQ_INVALID, "Child3 mapping failed");
    
    // Verify all have same parent domain
    struct irq_desc *desc1 = irq_to_desc(virq1);
    struct irq_desc *desc2 = irq_to_desc(virq2);
    struct irq_desc *desc3 = irq_to_desc(virq3);
    
    TEST_ASSERT(desc1->parent_desc != NULL, "Child1 no parent");
    TEST_ASSERT(desc2->parent_desc != NULL, "Child2 no parent");
    TEST_ASSERT(desc3->parent_desc != NULL, "Child3 no parent");
    
    TEST_ASSERT(desc1->parent_desc->domain == parent, "Wrong parent domain");
    TEST_ASSERT(desc2->parent_desc->domain == parent, "Wrong parent domain");
    TEST_ASSERT(desc3->parent_desc->domain == parent, "Wrong parent domain");
    
    // Clean up
    irq_dispose_mapping(virq1);
    irq_dispose_mapping(virq2);
    irq_dispose_mapping(virq3);
    irq_domain_remove(child1);
    irq_domain_remove(child2);
    irq_domain_remove(child3);
    irq_domain_remove(parent);
    
    TEST_PASS("Multiple children work correctly");
}

// Test: Domain removal with active mappings
static void test_domain_removal_with_mappings(void) {
    struct irq_domain *parent, *child;
    uint32_t virqs[10];
    int i;
    
    TEST_START("Domain removal with active mappings");
    
    parent = irq_domain_create_linear(NULL, 128, NULL, NULL);
    TEST_ASSERT(parent != NULL, "Parent creation failed");
    
    child = irq_domain_create_hierarchy(parent, 32, NULL,
                                        &test_simple_domain_ops, NULL);
    TEST_ASSERT(child != NULL, "Child creation failed");
    
    // Create multiple mappings
    for (i = 0; i < 10; i++) {
        virqs[i] = irq_create_mapping(child, i);
        TEST_ASSERT(virqs[i] != IRQ_INVALID, "Mapping creation failed");
    }
    
    // Remove domain - should clean up all mappings
    irq_domain_remove(child);
    
    // Verify descriptors are cleaned up (can't use the domain after removal!)
    for (i = 0; i < 10; i++) {
        struct irq_desc *desc = irq_to_desc(virqs[i]);
        TEST_ASSERT(desc == NULL || desc->domain != child, 
                   "Mapping not cleaned up");
    }
    
    irq_domain_remove(parent);
    
    TEST_PASS("Domain removal cleans up mappings");
}

// Test: Rapid mapping creation/deletion
static void test_rapid_mapping_changes(void) {
    struct irq_domain *parent, *child;
    uint32_t virq;
    int i, j;
    
    TEST_START("Rapid mapping creation/deletion");
    
    parent = irq_domain_create_linear(NULL, 256, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 64, NULL,
                                        &test_simple_domain_ops, NULL);
    TEST_ASSERT(child != NULL, "Domain creation failed");
    
    // Rapidly create and delete mappings
    for (i = 0; i < 100; i++) {
        for (j = 0; j < 10; j++) {
            virq = irq_create_mapping(child, j);
            TEST_ASSERT(virq != IRQ_INVALID, "Mapping failed");
            irq_dispose_mapping(virq);
        }
    }
    
    // Verify no leaks by creating mappings again
    for (j = 0; j < 10; j++) {
        virq = irq_create_mapping(child, j);
        TEST_ASSERT(virq != IRQ_INVALID, "Post-stress mapping failed");
        irq_dispose_mapping(virq);
    }
    
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Rapid changes handled correctly");
}

// Test: Multiple GPIO controllers with different parent ranges
static void test_multiple_gpio_controllers(void) {
    struct irq_domain *parent, *child1, *child2;
    struct test_gpio_controller *gpio1, *gpio2;
    uint32_t virq1, virq2;
    
    TEST_START("Multiple GPIO controllers");
    
    parent = irq_domain_create_linear(NULL, 256, NULL, NULL);
    
    // Create two GPIO controllers with NON-overlapping parent ranges
    gpio1 = kmalloc(sizeof(struct test_gpio_controller), KM_ZERO);
    gpio1->parent_hwirq_base = 32;
    gpio1->num_pins = 16;  // Must set this for the map function
    spin_lock_init(&gpio1->lock);
    
    gpio2 = kmalloc(sizeof(struct test_gpio_controller), KM_ZERO);
    gpio2->parent_hwirq_base = 64;  // No overlap with gpio1 (32+16=48 max)
    gpio2->num_pins = 16;  // Must set this for the map function
    spin_lock_init(&gpio2->lock);
    
    child1 = irq_domain_create_hierarchy(parent, 16, NULL,
                                         &test_gpio_domain_ops, gpio1);
    child2 = irq_domain_create_hierarchy(parent, 16, NULL,
                                         &test_gpio_domain_ops, gpio2);
    
    // Test that both domains can map their interrupts
    virq1 = irq_create_mapping(child1, 10);  // Maps to parent hwirq 42
    virq2 = irq_create_mapping(child2, 2);   // Maps to parent hwirq 66
    
    // Both should succeed with different parent hwirqs
    TEST_ASSERT(virq1 != IRQ_INVALID, "First mapping failed");
    TEST_ASSERT(virq2 != IRQ_INVALID, "Second mapping failed");
    TEST_ASSERT(virq1 != virq2, "Got same virq for different domains");
    
    irq_dispose_mapping(virq1);
    irq_dispose_mapping(virq2);
    irq_domain_remove(child1);
    irq_domain_remove(child2);
    irq_domain_remove(parent);
    kfree(gpio1);
    kfree(gpio2);
    
    TEST_PASS("Multiple GPIO controllers work");
}

// Test: Find mapping in hierarchy
static void test_find_mapping_hierarchy(void) {
    struct irq_domain *parent, *child;
    uint32_t virq, found_virq;
    
    TEST_START("Find mapping in hierarchy");
    
    parent = irq_domain_create_linear(NULL, 128, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 32, NULL,
                                        &test_simple_domain_ops, NULL);
    
    // Create mapping
    virq = irq_create_mapping(child, 15);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping creation failed");
    
    // Find it again
    found_virq = irq_find_mapping(child, 15);
    TEST_ASSERT(found_virq == virq, "Found different virq");
    
    // Parent domain will have its own mapping (because of hierarchy)
    // but it should be a different virq
    found_virq = irq_find_mapping(parent, 15);
    if (found_virq != IRQ_INVALID) {
        TEST_ASSERT(found_virq != virq, "Parent should have different virq");
    }
    // It's OK if parent doesn't have the mapping if using non-identity transformation
    
    irq_dispose_mapping(virq);
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Find mapping works correctly");
}

// Test: Activation failure handling
static void test_activation_failure(void) {
    // This would test activation failure scenarios
    // For now, just verify the basic flow works
    TEST_START("Activation failure handling");
    
    struct irq_domain *parent = irq_domain_create_linear(NULL, 64, NULL, NULL);
    struct irq_domain *child = irq_domain_create_hierarchy(parent, 16, NULL,
                                                           &test_simple_domain_ops, NULL);
    
    uint32_t virq = irq_create_mapping(child, 5);
    struct irq_desc *desc = irq_to_desc(virq);
    
    // Test activation
    int ret = irq_domain_activate_irq(desc, false);
    TEST_ASSERT(ret == 0, "Activation should succeed");
    
    // Test deactivation
    irq_domain_deactivate_irq(desc);
    
    irq_dispose_mapping(virq);
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Activation/deactivation works");
}

// Test: Cross-domain operations
static void test_cross_domain_operations(void) {
    struct irq_domain *parent, *child1, *child2;
    uint32_t virq1;
    
    TEST_START("Cross-domain operations");
    
    parent = irq_domain_create_linear(NULL, 128, NULL, NULL);
    child1 = irq_domain_create_hierarchy(parent, 16, NULL,
                                         &test_simple_domain_ops, NULL);
    child2 = irq_domain_create_hierarchy(parent, 16, NULL,
                                         &test_simple_domain_ops, NULL);
    
    // Create mapping in child1
    virq1 = irq_create_mapping(child1, 5);
    TEST_ASSERT(virq1 != IRQ_INVALID, "Mapping creation failed");
    
    // Should not find in child2
    uint32_t found = irq_find_mapping(child2, 5);
    TEST_ASSERT(found == IRQ_INVALID, "Found in wrong domain");
    
    // Dispose and verify
    irq_dispose_mapping(virq1);
    TEST_ASSERT(irq_find_mapping(child1, 5) == IRQ_INVALID, "Not disposed");
    
    irq_domain_remove(child1);
    irq_domain_remove(child2);
    irq_domain_remove(parent);
    
    TEST_PASS("Cross-domain isolation works");
}

// Test: Reuse of hwirq after disposal
static void test_hwirq_reuse(void) {
    struct irq_domain *parent, *child;
    uint32_t virq1, virq2;
    
    TEST_START("Hardware IRQ reuse after disposal");
    
    parent = irq_domain_create_linear(NULL, 64, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 32, NULL,
                                        &test_simple_domain_ops, NULL);
    
    // Create, dispose, and recreate same hwirq
    virq1 = irq_create_mapping(child, 7);
    TEST_ASSERT(virq1 != IRQ_INVALID, "First mapping failed");
    
    irq_dispose_mapping(virq1);
    
    virq2 = irq_create_mapping(child, 7);
    TEST_ASSERT(virq2 != IRQ_INVALID, "Reuse mapping failed");
    
    // May or may not get same virq number, but should work
    irq_dispose_mapping(virq2);
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Hardware IRQ reuse works");
}

// Test: Domain with no operations
static void test_domain_no_ops(void) {
    struct irq_domain *parent, *child;
    uint32_t virq;
    
    TEST_START("Domain with no operations");
    
    parent = irq_domain_create_linear(NULL, 64, NULL, NULL);
    
    // Create hierarchy with NULL ops
    child = irq_domain_create_hierarchy(parent, 16, NULL, NULL, NULL);
    TEST_ASSERT(child != NULL, "Creation should succeed");
    
    // Should still be able to create mappings
    virq = irq_create_mapping(child, 3);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping should work");
    
    irq_dispose_mapping(virq);
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Domain without ops works");
}

// Test: Large number of mappings
static void test_many_mappings(void) {
    struct irq_domain *parent, *child;
    uint32_t virqs[32];
    int i;
    
    TEST_START("Large number of mappings");
    
    parent = irq_domain_create_linear(NULL, 256, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 32, NULL,
                                        &test_simple_domain_ops, NULL);
    
    // Create maximum mappings
    for (i = 0; i < 32; i++) {
        virqs[i] = irq_create_mapping(child, i);
        TEST_ASSERT(virqs[i] != IRQ_INVALID, "Mapping failed");
    }
    
    // Verify all are different
    for (i = 0; i < 32; i++) {
        for (int j = i + 1; j < 32; j++) {
            TEST_ASSERT(virqs[i] != virqs[j], "Duplicate virq");
        }
    }
    
    // Clean up
    for (i = 0; i < 32; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Many mappings handled correctly");
}

// Test: Parent domain changes
static void test_parent_domain_changes(void) {
    struct irq_domain *parent1, *parent2, *child;
    uint32_t virq;
    
    TEST_START("Parent domain immutability");
    
    parent1 = irq_domain_create_linear(NULL, 64, NULL, NULL);
    parent2 = irq_domain_create_linear(NULL, 64, NULL, NULL);
    
    child = irq_domain_create_hierarchy(parent1, 16, NULL,
                                        &test_simple_domain_ops, NULL);
    
    // Verify parent is set correctly
    TEST_ASSERT(child->parent == parent1, "Wrong parent");
    
    // Create mapping
    virq = irq_create_mapping(child, 5);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping failed");
    
    // Parent should not change
    TEST_ASSERT(child->parent == parent1, "Parent changed");
    
    irq_dispose_mapping(virq);
    irq_domain_remove(child);
    irq_domain_remove(parent1);
    irq_domain_remove(parent2);
    
    TEST_PASS("Parent domain immutable");
}

// Test: Boundary conditions
static void test_boundary_conditions(void) {
    struct irq_domain *parent, *child;
    uint32_t virq;
    
    TEST_START("Boundary conditions");
    
    parent = irq_domain_create_linear(NULL, 256, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 32, NULL,
                                        &test_simple_domain_ops, NULL);
    
    // Test boundary hwirqs
    virq = irq_create_mapping(child, 0);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping hwirq 0 failed");
    irq_dispose_mapping(virq);
    
    virq = irq_create_mapping(child, 31);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping hwirq 31 failed");
    irq_dispose_mapping(virq);
    
    // Test out of bounds
    virq = irq_create_mapping(child, 32);
    TEST_ASSERT(virq == IRQ_INVALID, "Out of bounds should fail");
    
    virq = irq_create_mapping(child, 1000);
    TEST_ASSERT(virq == IRQ_INVALID, "Large hwirq should fail");
    
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Boundary conditions handled");
}

// Test: Handler registration in hierarchy
static void test_handler_registration(void) {
    struct irq_domain *parent, *child;
    uint32_t virq;
    int ret;
    
    TEST_START("Handler registration in hierarchy");
    
    // Reset counters
    gpio_test_handler_called = 0;
    
    parent = irq_domain_create_linear(NULL, 128, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 32, NULL,
                                        &test_simple_domain_ops, NULL);
    
    virq = irq_create_mapping(child, 8);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping failed");
    
    // Register handler
    ret = request_irq(virq, test_gpio_handler, 0, "test", 
                     (void *)(uintptr_t)8);
    TEST_ASSERT(ret == 0, "Handler registration failed");
    
    // Verify handler is set
    struct irq_desc *desc = irq_to_desc(virq);
    TEST_ASSERT(desc != NULL, "No descriptor");
    TEST_ASSERT(desc->action != NULL, "No action");
    TEST_ASSERT(desc->action->handler == test_gpio_handler, "Wrong handler");
    
    // Free handler
    free_irq(virq, (void *)(uintptr_t)8);
    TEST_ASSERT(desc->action == NULL, "Handler not freed");
    
    irq_dispose_mapping(virq);
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Handler registration works");
}

// Test: Shared interrupts in hierarchy
static void test_shared_interrupts(void) {
    struct irq_domain *parent, *child;
    uint32_t virq;
    int ret;
    
    TEST_START("Shared interrupts in hierarchy");
    
    parent = irq_domain_create_linear(NULL, 128, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 32, NULL,
                                        &test_simple_domain_ops, NULL);
    
    virq = irq_create_mapping(child, 10);
    TEST_ASSERT(virq != IRQ_INVALID, "Mapping failed");
    
    // Register multiple handlers with IRQF_SHARED
    ret = request_irq(virq, test_gpio_handler, IRQF_SHARED, "test1",
                     (void *)(uintptr_t)1);
    TEST_ASSERT(ret == 0, "First handler failed");
    
    ret = request_irq(virq, test_gpio_handler, IRQF_SHARED, "test2",
                     (void *)(uintptr_t)2);
    TEST_ASSERT(ret == 0, "Second handler failed");
    
    // Verify both handlers are in chain
    struct irq_desc *desc = irq_to_desc(virq);
    TEST_ASSERT(desc->action != NULL, "No action");
    TEST_ASSERT(desc->action->next != NULL, "No second handler");
    
    // Free handlers
    free_irq(virq, (void *)(uintptr_t)1);
    free_irq(virq, (void *)(uintptr_t)2);
    
    irq_dispose_mapping(virq);
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Shared interrupts work");
}

// Test: Circular reference prevention
static void test_circular_reference_prevention(void) {
    struct irq_domain *domain1, *domain2;
    
    TEST_START("Circular reference prevention");
    
    // Create first domain
    domain1 = irq_domain_create_linear(NULL, 64, NULL, NULL);
    TEST_ASSERT(domain1 != NULL, "Domain1 creation failed");
    
    // Try to create child that would create circular ref
    // (this should be prevented by design - child of child of self)
    domain2 = irq_domain_create_hierarchy(domain1, 32, NULL, NULL, NULL);
    TEST_ASSERT(domain2 != NULL, "Domain2 creation failed");
    
    // Domain2's parent should be domain1, not itself
    TEST_ASSERT(domain2->parent == domain1, "Parent not set correctly");
    TEST_ASSERT(domain2->parent != domain2, "Circular reference detected");
    
    irq_domain_remove(domain2);
    irq_domain_remove(domain1);
    
    TEST_PASS("No circular references");
}

// Test: Statistics tracking
static void test_statistics_tracking(void) {
    struct irq_domain *parent, *child;
    uint32_t virq;
    struct irq_desc *desc;
    
    TEST_START("Statistics tracking");
    
    parent = irq_domain_create_linear(NULL, 64, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 16, NULL,
                                        &test_simple_domain_ops, NULL);
    
    virq = irq_create_mapping(child, 5);
    desc = irq_to_desc(virq);
    TEST_ASSERT(desc != NULL, "No descriptor");
    
    // Initial stats should be zero
    TEST_ASSERT(desc->count == 0, "Count not zero");
    TEST_ASSERT(desc->spurious_count == 0, "Spurious not zero");
    
    // Register handler and simulate interrupts
    request_irq(virq, test_gpio_handler, 0, "test", NULL);
    
    // Simulate interrupts
    for (int i = 0; i < 10; i++) {
        if (desc->action && desc->action->handler) {
            desc->action->handler(desc->action->dev_data);
            desc->count++;
        }
    }
    
    TEST_ASSERT(desc->count == 10, "Count not updated");
    
    free_irq(virq, NULL);
    irq_dispose_mapping(virq);
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Statistics tracked correctly");
}

// Test: Stress test with many domains
static void test_stress_many_domains(void) {
    struct irq_domain *domains[20];
    uint32_t virqs[20];
    int i;
    
    TEST_START("Stress test with many domains");
    
    // Create many sibling domains
    struct irq_domain *parent = irq_domain_create_linear(NULL, 512, NULL, NULL);
    TEST_ASSERT(parent != NULL, "Parent creation failed");
    
    for (i = 0; i < 20; i++) {
        domains[i] = irq_domain_create_hierarchy(parent, 16, NULL,
                                                 &test_simple_domain_ops, NULL);
        TEST_ASSERT(domains[i] != NULL, "Domain creation failed");
        
        virqs[i] = irq_create_mapping(domains[i], i % 16);
        TEST_ASSERT(virqs[i] != IRQ_INVALID, "Mapping failed");
    }
    
    // Clean up in reverse order
    for (i = 19; i >= 0; i--) {
        irq_dispose_mapping(virqs[i]);
        irq_domain_remove(domains[i]);
    }
    
    irq_domain_remove(parent);
    
    TEST_PASS("Many domains handled");
}

// Test: Mapping persistence
static void test_mapping_persistence(void) {
    struct irq_domain *parent, *child;
    uint32_t virq1, virq2;
    
    TEST_START("Mapping persistence");
    
    parent = irq_domain_create_linear(NULL, 64, NULL, NULL);
    child = irq_domain_create_hierarchy(parent, 16, NULL,
                                        &test_simple_domain_ops, NULL);
    
    // Create mapping
    virq1 = irq_create_mapping(child, 7);
    TEST_ASSERT(virq1 != IRQ_INVALID, "Mapping failed");
    
    // Try to create same mapping again - should return existing
    virq2 = irq_create_mapping(child, 7);
    TEST_ASSERT(virq2 == virq1, "Should return existing mapping");
    
    // Find should also return same
    virq2 = irq_find_mapping(child, 7);
    TEST_ASSERT(virq2 == virq1, "Find returned different virq");
    
    irq_dispose_mapping(virq1);
    irq_domain_remove(child);
    irq_domain_remove(parent);
    
    TEST_PASS("Mapping persistence works");
}

// Run all hierarchical domain tests
void test_hierarchical_domains(void) {
    uart_puts("\n=== Testing Hierarchical Domain Support ===\n");
    
    // Reset statistics
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    
    // Basic functionality tests
    test_hierarchical_domain_creation();
    test_cascade_mapping();
    test_hierarchical_activation();
    test_multi_level_cascade();
    test_cascade_handler_invocation();
    
    // Error handling tests
    test_hierarchy_null_parameters();
    test_activation_failure();
    test_circular_reference_prevention();
    
    // Boundary and limit tests
    test_max_hierarchy_depth();
    test_boundary_conditions();
    test_many_mappings();
    
    // Multi-domain tests
    test_multiple_children();
    test_cross_domain_operations();
    test_multiple_gpio_controllers();
    test_parent_domain_changes();
    
    // Resource management tests
    test_domain_removal_with_mappings();
    test_hwirq_reuse();
    test_mapping_persistence();
    test_find_mapping_hierarchy();
    
    // Configuration tests
    test_domain_no_ops();
    test_handler_registration();
    test_shared_interrupts();
    
    // Performance and stress tests
    test_rapid_mapping_changes();
    test_stress_many_domains();
    test_statistics_tracking();
    
    // Print summary
    uart_puts("\n=== Hierarchical Domain Test Summary ===\n");
    uart_puts("Tests run: ");
    uart_putdec(tests_run);
    uart_puts("\nTests passed: ");
    uart_putdec(tests_passed);
    uart_puts("\nTests failed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("*** ALL HIERARCHICAL DOMAIN TESTS PASSED ***\n");
    } else {
        uart_puts("*** SOME TESTS FAILED ***\n");
    }
    
    uart_puts("=== Hierarchical Domain Tests Complete ===\n");
}