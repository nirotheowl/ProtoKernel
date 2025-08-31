/*
 * kernel/tests/irq/test_msi_spi_allocation.c
 * 
 * Test suite for MSI SPI allocation and doorbell handling
 */

#ifdef __aarch64__

#include <irqchip/arm-gic.h>
#include <irq/msi.h>
#include <uart.h>
#include <string.h>
#include <stddef.h>
#include <panic.h>

// Test statistics
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int assertions_run = 0;

// Helper macros for test assertions
#define TEST_ASSERT(condition, msg, ...) do { \
    assertions_run++; \
    if (!(condition)) { \
        uart_puts("  [FAIL] "); \
        uart_puts(msg); \
        uart_puts("\n"); \
        return -1; \
    } \
} while(0)

#define TEST_PASS 0
#define TEST_FAIL -1
#define TEST_SKIP -2

#define TEST_START(name) do { \
    uart_puts("\n=== " name " ===\n"); \
    tests_run = 0; \
    tests_passed = 0; \
    tests_failed = 0; \
} while(0)

#define TEST_RUN(test_func) do { \
    uart_puts("Running " #test_func "...\n"); \
    tests_run++; \
    int result = test_func(); \
    if (result == TEST_PASS) { \
        tests_passed++; \
        uart_puts("  [PASS]\n"); \
    } else if (result == TEST_SKIP) { \
        uart_puts("  [SKIP]\n"); \
    } else { \
        tests_failed++; \
        uart_puts("  [FAIL]\n"); \
    } \
} while(0)

#define TEST_END() do { \
    uart_puts("\nTest Summary:\n"); \
    uart_puts("  Tests run: "); \
    uart_putdec(tests_run); \
    uart_puts("\n  Passed: "); \
    uart_putdec(tests_passed); \
    uart_puts("\n  Failed: "); \
    uart_putdec(tests_failed); \
    uart_puts("\n\n"); \
} while(0)

// TEST_LOG removed - use uart_puts directly

// External references
extern struct gic_data *gic_primary;
extern int gic_msi_alloc_spi(struct gic_data *gic, uint32_t count, uint32_t *base_spi);
extern void gic_msi_free_spi(struct gic_data *gic, uint32_t base_spi, uint32_t count);
extern int gicv2_msi_test_doorbell(struct gic_data *gic, uint32_t spi_num);
extern int gicv3_msi_test_doorbell(struct gic_data *gic, uint32_t spi_num);

// Test: Single SPI allocation
static int test_single_spi_alloc(void) {
    if (!gic_primary) {
        uart_puts("  No GIC primary, skipping MSI tests\n");
        return TEST_SKIP;
    }
    
    // Check if MSI is supported
    if (!(gic_primary->msi_flags & (GIC_MSI_FLAGS_V2M | GIC_MSI_FLAGS_MBI))) {
        uart_puts("  MSI not supported on this GIC\n");
        return TEST_SKIP;
    }
    
    uint32_t base_spi = 0;
    
    // Allocate a single SPI
    int ret = gic_msi_alloc_spi(gic_primary, 1, &base_spi);
    TEST_ASSERT(ret == 0, "Failed to allocate single SPI");
    TEST_ASSERT(base_spi >= gic_primary->msi_spi_base, "Invalid base SPI");
    TEST_ASSERT(base_spi < gic_primary->msi_spi_base + gic_primary->msi_spi_count, 
                "Base SPI out of range");
    
    uart_puts("  Allocated single SPI: ");
    uart_putdec(base_spi);
    uart_puts("\n");
    
    // Free the allocated SPI
    gic_msi_free_spi(gic_primary, base_spi, 1);
    
    return TEST_PASS;
}

// Test: Multiple SPI allocation
static int test_multiple_spi_alloc(void) {
    if (!gic_primary) {
        return TEST_SKIP;
    }
    
    if (!(gic_primary->msi_flags & (GIC_MSI_FLAGS_V2M | GIC_MSI_FLAGS_MBI))) {
        return TEST_SKIP;
    }
    
    uint32_t base_spi = 0;
    
    // Test different allocation sizes (powers of 2 for MSI)
    uint32_t test_sizes[] = {2, 4, 8, 16, 32};
    
    for (int i = 0; i < 5; i++) {
        uint32_t count = test_sizes[i];
        
        // Skip if we don't have enough SPIs
        if (count > gic_primary->msi_spi_count) {
            uart_puts("  Skipping allocation of ");
            uart_putdec(count);
            uart_puts(" SPIs (only ");
            uart_putdec(gic_primary->msi_spi_count);
            uart_puts(" available)\n");
            continue;
        }
        
        int ret = gic_msi_alloc_spi(gic_primary, count, &base_spi);
        TEST_ASSERT(ret == 0, "Failed to allocate %u SPIs", count);
        
        uart_puts("  Allocated ");
        uart_putdec(count);
        uart_puts(" SPIs starting at ");
        uart_putdec(base_spi);
        uart_puts("\n");
        
        // Verify they're contiguous
        TEST_ASSERT(base_spi + count <= gic_primary->msi_spi_base + gic_primary->msi_spi_count,
                    "Allocation extends beyond MSI range");
        
        // Free the allocated SPIs
        gic_msi_free_spi(gic_primary, base_spi, count);
    }
    
    return TEST_PASS;
}

// Test: Allocation and free patterns
static int test_alloc_free_patterns(void) {
    if (!gic_primary) {
        return TEST_SKIP;
    }
    
    if (!(gic_primary->msi_flags & (GIC_MSI_FLAGS_V2M | GIC_MSI_FLAGS_MBI))) {
        return TEST_SKIP;
    }
    
    uint32_t base1 = 0, base2 = 0, base3 = 0;
    
    // Allocate 3 sets of SPIs
    int ret1 = gic_msi_alloc_spi(gic_primary, 4, &base1);
    TEST_ASSERT(ret1 == 0, "Failed to allocate first set");
    
    int ret2 = gic_msi_alloc_spi(gic_primary, 4, &base2);
    TEST_ASSERT(ret2 == 0, "Failed to allocate second set");
    
    int ret3 = gic_msi_alloc_spi(gic_primary, 4, &base3);
    TEST_ASSERT(ret3 == 0, "Failed to allocate third set");
    
    // Verify they don't overlap
    TEST_ASSERT(base2 >= base1 + 4, "Second allocation overlaps first");
    TEST_ASSERT(base3 >= base2 + 4, "Third allocation overlaps second");
    
    uart_puts("  Allocated SPIs: [");
    uart_putdec(base1);
    uart_puts("-");
    uart_putdec(base1+3);
    uart_puts("], [");
    uart_putdec(base2);
    uart_puts("-");
    uart_putdec(base2+3);
    uart_puts("], [");
    uart_putdec(base3);
    uart_puts("-");
    uart_putdec(base3+3);
    uart_puts("]\n");
    
    // Free middle allocation
    gic_msi_free_spi(gic_primary, base2, 4);
    
    // Allocate again - should reuse the freed space
    uint32_t base4 = 0;
    int ret4 = gic_msi_alloc_spi(gic_primary, 4, &base4);
    TEST_ASSERT(ret4 == 0, "Failed to allocate after free");
    TEST_ASSERT(base4 == base2, "Didn't reuse freed SPIs");
    
    // Clean up
    gic_msi_free_spi(gic_primary, base1, 4);
    gic_msi_free_spi(gic_primary, base4, 4);
    gic_msi_free_spi(gic_primary, base3, 4);
    
    return TEST_PASS;
}

// Test: Allocation exhaustion
static int test_allocation_exhaustion(void) {
    if (!gic_primary) {
        return TEST_SKIP;
    }
    
    if (!(gic_primary->msi_flags & (GIC_MSI_FLAGS_V2M | GIC_MSI_FLAGS_MBI))) {
        return TEST_SKIP;
    }
    
    // Try to allocate all available SPIs
    uint32_t total_spis = gic_primary->msi_spi_count;
    uint32_t base_spi = 0;
    
    int ret = gic_msi_alloc_spi(gic_primary, total_spis, &base_spi);
    TEST_ASSERT(ret == 0, "Failed to allocate all SPIs");
    
    uart_puts("  Allocated all ");
    uart_putdec(total_spis);
    uart_puts(" SPIs starting at ");
    uart_putdec(base_spi);
    uart_puts("\n");
    
    // Try to allocate one more - should fail
    uint32_t extra_base = 0;
    int ret2 = gic_msi_alloc_spi(gic_primary, 1, &extra_base);
    TEST_ASSERT(ret2 != 0, "Should have failed to allocate when exhausted");
    
    // Free all SPIs
    gic_msi_free_spi(gic_primary, base_spi, total_spis);
    
    // Should be able to allocate again
    ret = gic_msi_alloc_spi(gic_primary, 1, &base_spi);
    TEST_ASSERT(ret == 0, "Failed to allocate after freeing all");
    
    gic_msi_free_spi(gic_primary, base_spi, 1);
    
    return TEST_PASS;
}

// Test: MSI message composition
static int test_msi_compose(void) {
    if (!gic_primary) {
        return TEST_SKIP;
    }
    
    if (!(gic_primary->msi_flags & (GIC_MSI_FLAGS_V2M | GIC_MSI_FLAGS_MBI))) {
        return TEST_SKIP;
    }
    
    // Allocate an SPI for testing
    uint32_t base_spi = 0;
    int ret = gic_msi_alloc_spi(gic_primary, 1, &base_spi);
    TEST_ASSERT(ret == 0, "Failed to allocate SPI for compose test");
    
    // Create a mock MSI message
    struct msi_msg msg;
    memset(&msg, 0, sizeof(msg));
    
    // Use the compose callback
    if (gic_primary->ops && gic_primary->ops->msi_compose_msg) {
        gic_primary->ops->msi_compose_msg(gic_primary, base_spi,
                                          &msg.address_hi, &msg.address_lo,
                                          &msg.data);
        
        // Verify the message is reasonable
        TEST_ASSERT(msg.address_lo != 0 || msg.address_hi != 0, 
                    "MSI address is zero");
        TEST_ASSERT(msg.data == base_spi, "MSI data doesn't match SPI");
        
        uint64_t addr = ((uint64_t)msg.address_hi << 32) | msg.address_lo;
        uart_puts("  MSI message: addr=0x");
        uart_puthex(addr);
        uart_puts(", data=");
        uart_putdec(msg.data);
        uart_puts("\n");
        
        // Verify doorbell address matches
        TEST_ASSERT(addr == gic_primary->msi_doorbell_addr,
                    "MSI address doesn't match doorbell");
    }
    
    // Clean up
    gic_msi_free_spi(gic_primary, base_spi, 1);
    
    return TEST_PASS;
}

// Test: Doorbell simulation
static int test_doorbell_simulation(void) {
    if (!gic_primary) {
        return TEST_SKIP;
    }
    
    if (!(gic_primary->msi_flags & (GIC_MSI_FLAGS_V2M | GIC_MSI_FLAGS_MBI))) {
        return TEST_SKIP;
    }
    
    // Allocate an SPI for testing
    uint32_t base_spi = 0;
    int ret = gic_msi_alloc_spi(gic_primary, 1, &base_spi);
    TEST_ASSERT(ret == 0, "Failed to allocate SPI for doorbell test");
    
    uart_puts("  Testing doorbell for SPI ");
    uart_putdec(base_spi);
    uart_puts("\n");
    
    // Test doorbell based on GIC version
    if (gic_primary->version == GIC_V2) {
        ret = gicv2_msi_test_doorbell(gic_primary, base_spi);
        TEST_ASSERT(ret == 0, "GICv2m doorbell test failed");
    } else if (gic_primary->version == GIC_V3) {
        ret = gicv3_msi_test_doorbell(gic_primary, base_spi);
        TEST_ASSERT(ret == 0, "GICv3 MBI doorbell test failed");
    }
    
    // Check if the interrupt is pending (would be in real hardware)
    // This is just a sanity check - in real hardware the interrupt would fire
    
    // Clean up
    gic_msi_free_spi(gic_primary, base_spi, 1);
    
    return TEST_PASS;
}

// Test: Invalid operations
static int test_invalid_operations(void) {
    if (!gic_primary) {
        return TEST_SKIP;
    }
    
    if (!(gic_primary->msi_flags & (GIC_MSI_FLAGS_V2M | GIC_MSI_FLAGS_MBI))) {
        return TEST_SKIP;
    }
    
    uint32_t base_spi = 0;
    
    // Test NULL pointer
    int ret = gic_msi_alloc_spi(NULL, 1, &base_spi);
    TEST_ASSERT(ret != 0, "Should fail with NULL gic");
    
    ret = gic_msi_alloc_spi(gic_primary, 1, NULL);
    TEST_ASSERT(ret != 0, "Should fail with NULL base_spi");
    
    // Test zero count
    ret = gic_msi_alloc_spi(gic_primary, 0, &base_spi);
    TEST_ASSERT(ret != 0, "Should fail with zero count");
    
    // Test too many SPIs
    ret = gic_msi_alloc_spi(gic_primary, gic_primary->msi_spi_count + 1, &base_spi);
    TEST_ASSERT(ret != 0, "Should fail with too many SPIs");
    
    // Test freeing invalid range
    gic_msi_free_spi(gic_primary, 0, 1);  // Should just return
    gic_msi_free_spi(gic_primary, gic_primary->msi_spi_base + gic_primary->msi_spi_count, 1);
    
    // Test doorbell with invalid SPI
    if (gic_primary->version == GIC_V2) {
        ret = gicv2_msi_test_doorbell(gic_primary, 0);  // SPI 0 is usually not in MSI range
        TEST_ASSERT(ret != 0, "Should fail with invalid SPI");
    } else if (gic_primary->version == GIC_V3) {
        ret = gicv3_msi_test_doorbell(gic_primary, 0);
        TEST_ASSERT(ret != 0, "Should fail with invalid SPI");
    }
    
    return TEST_PASS;
}

// Main test function
int test_msi_spi_allocation(void) {
    TEST_START("MSI SPI Allocation Tests");
    
    TEST_RUN(test_single_spi_alloc);
    TEST_RUN(test_multiple_spi_alloc);
    TEST_RUN(test_alloc_free_patterns);
    TEST_RUN(test_allocation_exhaustion);
    TEST_RUN(test_msi_compose);
    TEST_RUN(test_doorbell_simulation);
    TEST_RUN(test_invalid_operations);
    
    TEST_END();
    return TEST_PASS;
}

#endif /* __aarch64__ */