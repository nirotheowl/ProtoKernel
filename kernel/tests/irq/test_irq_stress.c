#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irq/irq_alloc.h>
#include <memory/kmalloc.h>
#include <uart.h>
#include <string.h>

// Test results tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        uart_puts("[FAIL] "); \
        uart_puts(__func__); \
        uart_puts(": "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    uart_puts("[PASS] "); \
    uart_puts(__func__); \
    uart_puts(": "); \
    uart_puts(msg); \
    uart_puts("\n"); \
    tests_passed++; \
} while(0)

// Handler for stress testing
static uint64_t handler_call_counts[512];  // Track calls per IRQ

static void stress_handler(void *data) {
    int index = (int)(unsigned long)data;
    if (index >= 0 && index < 512) {
        handler_call_counts[index]++;
    }
}

// Stress test: Maximum allocations
static void stress_test_max_allocations(void) {
    uint32_t *virqs;
    int max_allocs = 512;  // Try to allocate many
    int allocated = 0;
    int i;
    
    uart_puts("\n[STRESS] Testing maximum allocations...\n");
    
    virqs = (uint32_t *)kmalloc(max_allocs * sizeof(uint32_t), 0);
    TEST_ASSERT(virqs != NULL, "Memory for test");
    
    // Allocate as many as possible
    for (i = 0; i < max_allocs; i++) {
        virqs[i] = virq_alloc();
        if (virqs[i] == IRQ_INVALID) {
            break;
        }
        allocated++;
    }
    
    uart_puts("  Allocated ");
    uart_putdec(allocated);
    uart_puts(" virqs\n");
    
    TEST_ASSERT(allocated > 100, "Should allocate significant number");
    TEST_PASS("Maximum allocation test");
    
    // Now free them all in reverse order
    for (i = allocated - 1; i >= 0; i--) {
        virq_free(virqs[i]);
    }
    
    // Verify all freed
    uint32_t count = virq_get_allocated_count();
    TEST_ASSERT(count <= 1, "All virqs freed (except reserved)");
    TEST_PASS("Maximum free test");
    
    kfree(virqs);
}

// Stress test: Rapid alloc/free cycles
static void stress_test_alloc_free_cycles(void) {
    uint32_t virq;
    int cycles = 1000;
    int i;
    
    uart_puts("\n[STRESS] Testing rapid alloc/free cycles...\n");
    
    for (i = 0; i < cycles; i++) {
        virq = virq_alloc();
        TEST_ASSERT(virq != IRQ_INVALID, "Allocation in cycle");
        virq_free(virq);
        
        if ((i + 1) % 100 == 0) {
            uart_puts("  Completed ");
            uart_putdec(i + 1);
            uart_puts(" cycles\n");
        }
    }
    
    TEST_PASS("1000 alloc/free cycles");
}

// Stress test: Many domains
static void stress_test_many_domains(void) {
    struct irq_domain *domains[32];
    int num_domains = 32;
    int i;
    
    uart_puts("\n[STRESS] Testing many domains...\n");
    
    // Create many domains
    for (i = 0; i < num_domains; i++) {
        domains[i] = irq_domain_create_linear(NULL, 16 + i, NULL, (void *)(unsigned long)i);
        if (!domains[i]) {
            num_domains = i;
            break;
        }
    }
    
    uart_puts("  Created ");
    uart_putdec(num_domains);
    uart_puts(" domains\n");
    
    TEST_ASSERT(num_domains >= 16, "Should create many domains");
    TEST_PASS("Many domains created");
    
    // Create a mapping in each
    for (i = 0; i < num_domains; i++) {
        uint32_t virq = irq_create_mapping(domains[i], i % 16);
        TEST_ASSERT(virq != IRQ_INVALID, "Mapping in each domain");
        irq_dispose_mapping(virq);
    }
    TEST_PASS("Mappings in all domains");
    
    // Remove all domains
    for (i = 0; i < num_domains; i++) {
        irq_domain_remove(domains[i]);
    }
    TEST_PASS("All domains removed");
}

// Stress test: Large bulk allocations
static void stress_test_bulk_operations(void) {
    struct irq_domain *domain;
    int virq_bases[16];
    int sizes[16] = {1, 2, 4, 8, 16, 32, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19};
    int allocated = 0;
    int i;
    
    uart_puts("\n[STRESS] Testing bulk allocation patterns...\n");
    
    domain = irq_domain_create_linear(NULL, 512, NULL, NULL);
    TEST_ASSERT(domain != NULL, "Large domain for bulk test");
    
    // Allocate various sizes
    for (i = 0; i < 16; i++) {
        virq_bases[i] = irq_domain_alloc_irqs(domain, sizes[i], NULL, NULL);
        if (virq_bases[i] > 0) {
            allocated++;
        }
    }
    
    uart_puts("  Allocated ");
    uart_putdec(allocated);
    uart_puts(" bulk ranges\n");
    
    TEST_ASSERT(allocated >= 10, "Most bulk allocations succeeded");
    TEST_PASS("Various bulk sizes");
    
    // Free them all
    for (i = 0; i < 16; i++) {
        if (virq_bases[i] > 0) {
            irq_domain_free_irqs(virq_bases[i], sizes[i]);
        }
    }
    TEST_PASS("Bulk free completed");
    
    irq_domain_remove(domain);
}

// Stress test: Handler registration/unregistration
static void stress_test_handler_registration(void) {
    struct irq_domain *domain;
    uint32_t virqs[64];
    int num_irqs = 64;
    int i, j;
    int ret;
    
    uart_puts("\n[STRESS] Testing handler registration...\n");
    
    // Initialize handler counts
    memset(handler_call_counts, 0, sizeof(handler_call_counts));
    
    // Create domain and mappings
    domain = irq_domain_create_linear(NULL, 128, NULL, NULL);
    for (i = 0; i < num_irqs; i++) {
        virqs[i] = irq_create_mapping(domain, i);
        TEST_ASSERT(virqs[i] != IRQ_INVALID, "Mapping created");
    }
    
    // Register handlers
    for (i = 0; i < num_irqs; i++) {
        ret = request_irq(virqs[i], stress_handler, 0, "stress", (void *)(unsigned long)i);
        TEST_ASSERT(ret == 0, "Handler registered");
    }
    TEST_PASS("64 handlers registered");
    
    // Trigger each interrupt multiple times
    for (j = 0; j < 10; j++) {
        for (i = 0; i < num_irqs; i++) {
            generic_handle_irq(virqs[i]);
        }
    }
    
    // Verify all were called
    for (i = 0; i < num_irqs; i++) {
        TEST_ASSERT(handler_call_counts[i] == 10, "Handler called 10 times");
    }
    TEST_PASS("All handlers called correctly");
    
    // Unregister all
    for (i = 0; i < num_irqs; i++) {
        free_irq(virqs[i], (void *)(unsigned long)i);
    }
    TEST_PASS("All handlers unregistered");
    
    // Clean up
    for (i = 0; i < num_irqs; i++) {
        irq_dispose_mapping(virqs[i]);
    }
    irq_domain_remove(domain);
}

// Stress test: Fragmentation handling
static void stress_test_fragmentation(void) {
    uint32_t virqs[256];
    int num_allocs = 256;
    int i;
    int pattern;
    
    uart_puts("\n[STRESS] Testing fragmentation patterns...\n");
    
    // Allocate many virqs
    for (i = 0; i < num_allocs; i++) {
        virqs[i] = virq_alloc();
        if (virqs[i] == IRQ_INVALID) {
            num_allocs = i;
            break;
        }
    }
    
    uart_puts("  Allocated ");
    uart_putdec(num_allocs);
    uart_puts(" virqs for fragmentation\n");
    
    // Create fragmentation pattern 1: Free every other
    for (i = 0; i < num_allocs; i += 2) {
        virq_free(virqs[i]);
    }
    TEST_PASS("Pattern 1: Every other freed");
    
    // Try to allocate ranges in fragmented space
    uint32_t range = virq_alloc_range(2);
    if (range != IRQ_INVALID) {
        virq_free_range(range, 2);
    }
    
    // Re-allocate freed ones
    for (i = 0; i < num_allocs; i += 2) {
        virqs[i] = virq_alloc();
        TEST_ASSERT(virqs[i] != IRQ_INVALID, "Re-allocation in fragments");
    }
    TEST_PASS("Re-allocated in fragments");
    
    // Pattern 2: Free in blocks
    for (pattern = 0; pattern < 4; pattern++) {
        for (i = pattern * 64; i < (pattern + 1) * 64 && i < num_allocs; i++) {
            virq_free(virqs[i]);
        }
        
        // Try to allocate a large range
        range = virq_alloc_range(32);
        if (range != IRQ_INVALID) {
            TEST_PASS("Large range allocated in freed block");
            virq_free_range(range, 32);
        }
    }
    
    TEST_PASS("Block fragmentation handled");
}

// Stress test: Shared interrupt scaling
static void stress_test_shared_interrupts(void) {
    struct irq_domain *domain;
    uint32_t virq;
    int num_handlers = 32;
    int i;
    int ret;
    
    uart_puts("\n[STRESS] Testing shared interrupt scaling...\n");
    
    // Create domain and mapping
    domain = irq_domain_create_linear(NULL, 8, NULL, NULL);
    virq = irq_create_mapping(domain, 0);
    
    // Register many shared handlers
    for (i = 0; i < num_handlers; i++) {
        ret = request_irq(virq, stress_handler, IRQF_SHARED, "shared", 
                         (void *)(unsigned long)i);
        if (ret != 0) {
            num_handlers = i;
            break;
        }
    }
    
    uart_puts("  Registered ");
    uart_putdec(num_handlers);
    uart_puts(" shared handlers\n");
    
    TEST_ASSERT(num_handlers >= 16, "Many shared handlers");
    TEST_PASS("Shared handler scaling");
    
    // Reset counts and trigger interrupt
    memset(handler_call_counts, 0, sizeof(handler_call_counts));
    generic_handle_irq(virq);
    
    // Verify all were called
    for (i = 0; i < num_handlers; i++) {
        TEST_ASSERT(handler_call_counts[i] == 1, "Shared handler called");
    }
    TEST_PASS("All shared handlers invoked");
    
    // Free all handlers
    for (i = 0; i < num_handlers; i++) {
        free_irq(virq, (void *)(unsigned long)i);
    }
    
    irq_dispose_mapping(virq);
    irq_domain_remove(domain);
}

// Stress test: Domain creation/destruction cycles
static void stress_test_domain_cycles(void) {
    struct irq_domain *domain;
    uint32_t virq;
    int cycles = 100;
    int i;
    
    uart_puts("\n[STRESS] Testing domain create/destroy cycles...\n");
    
    for (i = 0; i < cycles; i++) {
        // Create domain
        domain = irq_domain_create_linear(NULL, 32, NULL, NULL);
        TEST_ASSERT(domain != NULL, "Domain created");
        
        // Create some mappings
        virq = irq_create_mapping(domain, 5);
        TEST_ASSERT(virq != IRQ_INVALID, "Mapping created");
        irq_dispose_mapping(virq);
        
        virq = irq_create_mapping(domain, 10);
        TEST_ASSERT(virq != IRQ_INVALID, "Mapping created");
        irq_dispose_mapping(virq);
        
        // Remove domain
        irq_domain_remove(domain);
        
        if ((i + 1) % 20 == 0) {
            uart_puts("  Completed ");
            uart_putdec(i + 1);
            uart_puts(" cycles\n");
        }
    }
    
    TEST_PASS("100 domain cycles completed");
}

// Main stress test runner
void run_irq_stress_tests(void) {
    uart_puts("\n========================================\n");
    uart_puts("   IRQ SUBSYSTEM STRESS TESTS\n");
    uart_puts("========================================\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Initialize IRQ subsystem
    irq_init();
    
    // Run stress tests
    stress_test_max_allocations();
    stress_test_alloc_free_cycles();
    stress_test_many_domains();
    stress_test_bulk_operations();
    stress_test_handler_registration();
    stress_test_fragmentation();
    stress_test_shared_interrupts();
    stress_test_domain_cycles();
    
    // Summary
    uart_puts("\n================================\n");
    uart_puts("Stress Test Summary:\n");
    uart_puts("  PASSED: ");
    uart_putdec(tests_passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("  System handled stress successfully!\n");
    }
    uart_puts("================================\n");
}