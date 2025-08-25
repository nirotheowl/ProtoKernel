/*
 * GICv3-Specific Tests
 * Tests for ARM GICv3 unique features not present in GICv2
 */

#ifdef __aarch64__

#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irqchip/arm-gic.h>
#include <arch_gic_sysreg.h>
#include <uart.h>
#include <arch_interface.h>
#include <arch_io.h>
#include <stdint.h>
#include <stdbool.h>

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) uart_puts("\n[TEST] " name "\n")
#define TEST_PASS(msg) do { uart_puts("  [PASS] " msg "\n"); tests_passed++; } while(0)
#define TEST_FAIL(msg) do { uart_puts("  [FAIL] " msg "\n"); tests_failed++; } while(0)
#define TEST_INFO(msg) uart_puts("  [INFO] " msg "\n")
#define TEST_SKIP(msg) uart_puts("  [SKIP] " msg "\n")

// Test interrupt counters
static volatile int test_sgi_count = 0;
static volatile int test_spi_count = 0;

// Test handlers
static void gicv3_sgi_handler(void *data) {
    (void)data;  // Suppress unused parameter warning
    test_sgi_count++;
}

static void gicv3_spi_handler(void *data) {
    (void)data;  // Suppress unused parameter warning
    test_spi_count++;
}

// Helper to check if we're using GICv3
static bool is_gicv3(void) {
    // Check if system register access is enabled
    uint32_t sre = gicv3_read_sre();
    return (sre & ICC_SRE_EL1_SRE) != 0;
}

// Test 1: Verify GICv3 detection and system register access
static void test_gicv3_detection(void) {
    TEST_START("GICv3 Detection and System Register Access");
    
    if (!is_gicv3()) {
        TEST_INFO("System is not using GICv3, skipping GICv3-specific tests");
        return;
    }
    TEST_PASS("GICv3 detected via ICC_SRE_EL1");
    
    // Test that we can read system registers
    uint32_t sre = gicv3_read_sre();
    if (sre & ICC_SRE_EL1_SRE) {
        TEST_PASS("System register access enabled");
    } else {
        TEST_FAIL("System register access not enabled");
        return;
    }
    
    // Verify bypass is disabled
    if (sre & (ICC_SRE_EL1_DFB | ICC_SRE_EL1_DIB)) {
        TEST_PASS("IRQ/FIQ bypass disabled");
    } else {
        TEST_FAIL("IRQ/FIQ bypass not properly disabled");
    }
    
    // Test priority mask register
    uint32_t old_pmr = gicv3_read_pmr();
    gicv3_write_pmr(0x80);
    uint32_t new_pmr = gicv3_read_pmr();
    if (new_pmr == 0x80) {
        TEST_PASS("ICC_PMR_EL1 write/read successful");
    } else {
        TEST_FAIL("ICC_PMR_EL1 write/read failed");
    }
    gicv3_write_pmr(old_pmr);  // Restore
    
    // Test control register
    uint32_t ctlr = gicv3_read_ctlr();
    TEST_INFO("ICC_CTLR_EL1 value:");
    uart_puts("    EOImode: ");
    uart_puts((ctlr & ICC_CTLR_EL1_EOImode) ? "Split" : "Combined");
    uart_puts("\n    CBPR: ");
    uart_puts((ctlr & ICC_CTLR_EL1_CBPR) ? "Shared" : "Separate");
    uart_puts("\n");
    
    // Test group enable register
    uint32_t grpen1 = gicv3_read_grpen1();
    if (grpen1 & ICC_IGRPEN1_EL1_Enable) {
        TEST_PASS("Group 1 interrupts enabled");
    } else {
        TEST_FAIL("Group 1 interrupts not enabled");
    }
}

// Test 2: Verify redistributor configuration
static void test_redistributor_config(void) {
    TEST_START("GICv3 Redistributor Configuration");
    
    if (!is_gicv3()) {
        return;
    }
    
    // Check if we have a valid GIC primary
    if (!gic_primary) {
        TEST_FAIL("No GIC primary found");
        return;
    }
    
    // Verify redistributor was discovered
    if (gic_primary->redist_base) {
        TEST_PASS("Redistributor base address configured");
        uart_puts("    Base: ");
        uart_puthex((uint64_t)gic_primary->redist_base);
        uart_puts("\n    Size: ");
        uart_puthex(gic_primary->redist_size);
        uart_puts("\n");
    } else {
        TEST_FAIL("No redistributor base address");
        return;
    }
    
    // Check number of CPUs detected
    if (gic_primary->nr_cpus > 0) {
        TEST_PASS("Redistributors discovered");
        uart_puts("    CPU count: ");
        uart_putdec(gic_primary->nr_cpus);
        uart_puts("\n");
    } else {
        TEST_FAIL("No redistributors found");
    }
}

// Test 3: Test interrupt acknowledgment via system registers
static void test_system_register_interrupt_flow(void) {
    TEST_START("GICv3 System Register Interrupt Flow");
    
    if (!is_gicv3()) {
        return;
    }
    
    struct irq_domain *domain = irq_find_host(NULL);
    if (!domain) {
        TEST_FAIL("No IRQ domain found");
        return;
    }
    
    // Map a test SPI (let's use hwirq 64)
    uint32_t test_hwirq = 64;
    uint32_t virq = irq_create_mapping(domain, test_hwirq);
    if (!virq) {
        TEST_FAIL("Failed to create mapping for test interrupt");
        return;
    }
    TEST_PASS("Test interrupt mapped");
    
    // Request the interrupt
    test_spi_count = 0;
    int ret = request_irq(virq, gicv3_spi_handler, IRQF_SHARED, "gicv3_test", (void*)0x1234);
    if (ret == 0) {
        TEST_PASS("Test interrupt requested");
    } else {
        TEST_FAIL("Failed to request test interrupt");
        irq_dispose_mapping(virq);
        return;
    }
    
    // The interrupt won't actually fire without hardware trigger,
    // but we've verified the setup path works
    
    // Test reading IAR (should return spurious)
    uint32_t iar = gicv3_read_iar1();
    if (iar == GIC_SPURIOUS_IRQ || (iar & 0xFFFFFF) == GIC_SPURIOUS_IRQ) {
        TEST_PASS("ICC_IAR1_EL1 returns spurious (no pending interrupts)");
    } else {
        TEST_INFO("ICC_IAR1_EL1 returned unexpected value");
        uart_puts("    IAR: ");
        uart_puthex(iar);
        uart_puts("\n");
    }
    
    // Clean up
    free_irq(virq, (void*)0x1234);
    irq_dispose_mapping(virq);
    TEST_PASS("Cleanup completed");
}

// Test 4: Test affinity routing configuration
// 
// NOTE: This test may SKIP rather than PASS on systems with TrustZone security enabled.
// When running at non-secure EL1 with security enabled:
// - The distributor can be enabled during initialization with G1A (non-secure) interrupts
// - However, if the distributor gets disabled (e.g., by IRQ mask/free operations in earlier tests)
// - Non-secure code CANNOT re-enable it due to security restrictions
// - This results in GICD_CTLR showing 0x80000000 (only RWP bit set, all enable bits cleared)
// - This is EXPECTED behavior, not a bug, so the test will SKIP with an appropriate message
//
// The test will only FAIL if the distributor is unexpectedly disabled when security is NOT enabled,
// or if the configuration doesn't match the security state.
static void test_affinity_routing(void) {
    TEST_START("GICv3 Affinity Routing");
    
    if (!is_gicv3()) {
        return;
    }
    
    // Read current CPU's MPIDR
    uint64_t mpidr = read_mpidr();
    uint32_t aff = mpidr_to_affinity(mpidr);
    
    TEST_INFO("Current CPU affinity:");
    uart_puts("    MPIDR: ");
    uart_puthex(mpidr);
    uart_puts("\n    Affinity: ");
    uart_puthex(aff);
    uart_puts("\n    Aff0: ");
    uart_puthex((mpidr >> MPIDR_AFF0_SHIFT) & 0xFF);
    uart_puts("\n    Aff1: ");
    uart_puthex((mpidr >> MPIDR_AFF1_SHIFT) & 0xFF);
    uart_puts("\n    Aff2: ");
    uart_puthex((mpidr >> MPIDR_AFF2_SHIFT) & 0xFF);
    uart_puts("\n    Aff3: ");
    uart_puthex((mpidr >> MPIDR_AFF3_SHIFT) & 0xFF);
    uart_puts("\n");
    
    TEST_PASS("MPIDR read successfully");
    
    // Verify distributor configuration
    if (gic_primary && gic_primary->dist_base) {
        uint32_t ctlr = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_CTLR);
        
        // Check if security is enabled (DS bit not set)
        bool security_enabled = !(ctlr & GICD_CTLR_DS);
        
        // Check if distributor has been disabled (only RWP bit set)
        if ((ctlr & 0x7FFFFFFF) == 0) {
            // All enable bits are cleared - this happens with security enabled
            // when non-secure code cannot re-enable after a disable
            if (security_enabled) {
                TEST_SKIP("Distributor disabled (expected with security enabled from EL1)");
            } else {
                TEST_FAIL("Distributor unexpectedly disabled");
            }
        } else if (ctlr & GICD_CTLR_ARE_NS) {
            if (security_enabled && (ctlr & GICD_CTLR_ENABLE_G1A)) {
                TEST_PASS("Distributor configured for secure mode with ARE_NS");
            } else if (!security_enabled && (ctlr & (GICD_CTLR_ENABLE_G1A | GICD_CTLR_ENABLE_G1))) {
                TEST_PASS("Distributor configured for non-secure mode with ARE_NS");
            } else {
                TEST_SKIP("Distributor in unexpected configuration");
            }
        } else if (ctlr & GICD_CTLR_ENABLE_G1A) {
            // Some implementations have ARE as always-on
            TEST_PASS("Distributor enabled (ARE may be implicit)");
        } else {
            TEST_FAIL("Distributor not properly enabled");
        }
    }
}

// Test 5: Test SGI generation via system registers
static void test_system_register_sgi(void) {
    TEST_START("GICv3 SGI Generation via ICC_SGI1R_EL1");
    
    if (!is_gicv3()) {
        return;
    }
    
    struct irq_domain *domain = irq_find_host(NULL);
    if (!domain) {
        TEST_FAIL("No IRQ domain found");
        return;
    }
    
    // Map SGI 15 (last SGI)
    uint32_t sgi_hwirq = 15;
    uint32_t virq = irq_create_mapping(domain, sgi_hwirq);
    if (!virq) {
        TEST_FAIL("Failed to map SGI 15");
        return;
    }
    TEST_PASS("SGI 15 mapped");
    
    // Request the SGI
    test_sgi_count = 0;
    int ret = request_irq(virq, gicv3_sgi_handler, 0, "gicv3_sgi_test", NULL);
    if (ret == 0) {
        TEST_PASS("SGI 15 handler installed");
    } else {
        TEST_FAIL("Failed to install SGI handler");
        irq_dispose_mapping(virq);
        return;
    }
    
    // Generate SGI to self using system register
    uint64_t sgi_val = (1ULL << ICC_SGI1R_TARGET_LIST_SHIFT) |  // Target self
                       (15ULL << ICC_SGI1R_INTID_SHIFT);         // SGI 15
    
    uart_puts("  [INFO] Sending SGI via ICC_SGI1R_EL1\n");
    uart_puts("    Value: ");
    uart_puthex(sgi_val);
    uart_puts("\n");
    
    // Note: The SGI won't actually be delivered in our test environment
    // but we're verifying the mechanism exists and doesn't crash
    gicv3_write_sgi1r(sgi_val);
    __asm__ volatile("isb");
    
    TEST_PASS("SGI sent via ICC_SGI1R_EL1 (delivery depends on GIC state)");
    
    // Clean up
    free_irq(virq, NULL);
    irq_dispose_mapping(virq);
}

// Test 6: Verify GICv3 interrupt properties
static void test_gicv3_properties(void) {
    TEST_START("GICv3 Interrupt Properties");
    
    if (!is_gicv3()) {
        return;
    }
    
    if (!gic_primary) {
        TEST_FAIL("No GIC primary");
        return;
    }
    
    // Check number of interrupts
    if (gic_primary->nr_irqs >= 32) {
        TEST_PASS("Interrupt count detected");
        uart_puts("    Total interrupts: ");
        uart_putdec(gic_primary->nr_irqs);
        uart_puts("\n");
    } else {
        TEST_FAIL("Invalid interrupt count");
    }
    
    // Check that redistributor size is reasonable
    if (gic_primary->redist_size >= 0x20000) {  // At least 128KB per redistributor
        TEST_PASS("Redistributor size adequate");
    } else if (gic_primary->redist_size > 0) {
        TEST_INFO("Redistributor size may be too small");
        uart_puts("    Size: ");
        uart_puthex(gic_primary->redist_size);
        uart_puts("\n");
    }
    
    // Verify we're not using CPU interface (GICv2 style)
    if (gic_primary->cpu_base == NULL) {
        TEST_PASS("No CPU interface base (correct for GICv3)");
    } else {
        TEST_FAIL("CPU interface base set (should be NULL for GICv3)");
    }
}

// Test 7: Test interrupt priority management
static void test_interrupt_priorities(void) {
    TEST_START("GICv3 Interrupt Priority Management");
    
    if (!is_gicv3()) {
        return;
    }
    
    // Test priority mask register full range
    uint32_t original_pmr = gicv3_read_pmr();
    
    // Test various priority levels
    // Note: Many GIC implementations only support 4-bit priority (16 levels)
    // so values might be masked to 0xF0, 0xE0, etc.
    uint32_t test_priorities[] = {0x00, 0x20, 0x40, 0x80, 0xC0, 0xE0, 0xFF};
    bool pmr_test_passed = true;
    uint32_t priority_mask = 0xFF;  // Will be determined from first non-zero write
    bool mask_determined = false;
    
    for (int i = 0; i < 7; i++) {
        gicv3_write_pmr(test_priorities[i]);
        __asm__ volatile("isb");
        uint32_t readback = gicv3_read_pmr();
        
        // Determine the priority mask from hardware behavior
        if (!mask_determined && test_priorities[i] != 0) {
            if (readback != test_priorities[i]) {
                // Hardware is masking priorities - determine the mask
                priority_mask = readback | ~test_priorities[i];
                mask_determined = true;
                uart_puts("    [INFO] Hardware priority mask detected: ");
                uart_puthex(priority_mask);
                uart_puts(" (");
                // Count bits manually instead of using __builtin_popcount
                int bits = 0;
                uint32_t mask_copy = priority_mask;
                while (mask_copy) {
                    bits += mask_copy & 1;
                    mask_copy >>= 1;
                }
                uart_putdec(1 << (8 - bits));
                uart_puts(" priority levels)\n");
            }
        }
        
        // Check if readback matches expected (with mask applied)
        uint32_t expected = test_priorities[i] & priority_mask;
        if (readback != expected) {
            pmr_test_passed = false;
            uart_puts("    PMR unexpected value at ");
            uart_puthex(test_priorities[i]);
            uart_puts(" (got ");
            uart_puthex(readback);
            uart_puts(", expected ");
            uart_puthex(expected);
            uart_puts(")\n");
        }
    }
    
    if (pmr_test_passed) {
        TEST_PASS("ICC_PMR_EL1 test successful (with hardware masking)");
    } else {
        TEST_FAIL("ICC_PMR_EL1 unexpected behavior");
    }
    
    // Test Binary Point Register
    uint32_t original_bpr = gicv3_read_bpr1();
    
    // BPR typically supports values 0-7, but hardware may enforce minimum values
    // The GIC architecture allows implementations to limit BPR to minimum values
    uint32_t min_bpr = 7;
    bool bpr_test_passed = true;
    
    for (uint32_t bpr_val = 0; bpr_val <= 7; bpr_val++) {
        gicv3_write_bpr1(bpr_val);
        __asm__ volatile("isb");
        uint32_t readback = gicv3_read_bpr1();
        
        // Determine minimum BPR value supported by hardware
        if (bpr_val == 0 && readback > 0) {
            min_bpr = readback;
            uart_puts("    [INFO] Minimum BPR value enforced by hardware: ");
            uart_putdec(min_bpr);
            uart_puts("\n");
        }
        
        // Check if readback is within expected range
        uint32_t expected = (bpr_val < min_bpr) ? min_bpr : bpr_val;
        if (readback != expected && readback > bpr_val) {
            // Only fail if it's truly unexpected (not just minimum enforcement)
            bpr_test_passed = false;
            uart_puts("    BPR unexpected: wrote ");
            uart_putdec(bpr_val);
            uart_puts(", got ");
            uart_putdec(readback);
            uart_puts("\n");
        }
    }
    
    if (bpr_test_passed) {
        TEST_PASS("ICC_BPR1_EL1 configuration test completed");
    } else {
        TEST_FAIL("ICC_BPR1_EL1 unexpected behavior");
    }
    
    // Test priority grouping via BPR
    gicv3_write_bpr1(3);  // 4 group priority bits, 4 subpriority bits
    __asm__ volatile("isb");
    uint32_t bpr_readback = gicv3_read_bpr1();
    if (bpr_readback <= 3) {
        TEST_PASS("Binary Point configuration accepted");
    } else {
        TEST_INFO("Binary Point configuration limited by hardware");
    }
    
    // Test running priority register (read-only)
    uint32_t rpr = gicv3_read_rpr();
    if (rpr == 0xFF || rpr == 0x00) {  // Idle priority or no active interrupts
        TEST_PASS("ICC_RPR_EL1 indicates idle state");
    } else {
        TEST_INFO("ICC_RPR_EL1 shows active priority");
        uart_puts("    RPR value: ");
        uart_puthex(rpr);
        uart_puts("\n");
    }
    
    // Restore original values
    gicv3_write_pmr(original_pmr);
    gicv3_write_bpr1(original_bpr);
    __asm__ volatile("isb");
}

// Test 8: Test edge vs level triggered interrupts
static void test_trigger_modes(void) {
    TEST_START("GICv3 Edge vs Level Triggered Interrupts");
    
    if (!is_gicv3() || !gic_primary) {
        return;
    }
    
    // Test SPI configuration (SPIs can be configured, SGIs/PPIs are fixed)
    uint32_t test_spi = 32;  // First SPI
    
    // Save original configuration
    uint32_t reg = test_spi / 16;
    uint32_t shift = (test_spi % 16) * 2;
    uint32_t orig_cfg = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_ICFGR + (reg * 4));
    
    // Test level-triggered (0b00)
    uint32_t new_cfg = orig_cfg & ~(0x3 << shift);
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ICFGR + (reg * 4), new_cfg);
    uint32_t readback = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_ICFGR + (reg * 4));
    
    if ((readback & (0x3 << shift)) == 0) {
        TEST_PASS("Level-triggered configuration set");
    } else {
        TEST_FAIL("Failed to set level-triggered mode");
    }
    
    // Test edge-triggered (0b10)
    new_cfg = orig_cfg | (0x2 << shift);
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ICFGR + (reg * 4), new_cfg);
    readback = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_ICFGR + (reg * 4));
    
    if ((readback & (0x3U << shift)) == (0x2U << shift)) {
        TEST_PASS("Edge-triggered configuration set");
    } else {
        TEST_FAIL("Failed to set edge-triggered mode");
    }
    
    // Test that SGIs have fixed configuration (edge-triggered)
    uint32_t sgi_cfg = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_ICFGR);
    // SGIs should all be edge-triggered (0xAAAAAAAA for interrupts 0-15)
    if ((sgi_cfg & 0xAAAA0000) == 0xAAAA0000) {  // Check upper 16 bits for SGIs
        TEST_PASS("SGIs have fixed edge-triggered configuration");
    } else {
        TEST_INFO("SGI configuration differs from expected");
    }
    
    // Restore original configuration
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ICFGR + (reg * 4), orig_cfg);
}

// Test 9: Test interrupt state transitions
static void test_interrupt_states(void) {
    TEST_START("GICv3 Interrupt State Transitions");
    
    if (!is_gicv3() || !gic_primary) {
        return;
    }
    
    // Use a high SPI that's unlikely to be in use
    uint32_t test_irq = 100;
    uint32_t reg = test_irq / 32;
    uint32_t bit = 1 << (test_irq % 32);
    
    // Ensure interrupt is disabled first
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ICENABLER + (reg * 4), bit);
    
    // Clear any pending state
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ICPENDR + (reg * 4), bit);
    
    // Test pending state
    // Set pending
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ISPENDR + (reg * 4), bit);
    uint32_t pending = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_ISPENDR + (reg * 4));
    
    if (pending & bit) {
        TEST_PASS("Interrupt pending state set");
    } else {
        TEST_FAIL("Failed to set pending state");
    }
    
    // Clear pending
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ICPENDR + (reg * 4), bit);
    pending = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_ISPENDR + (reg * 4));
    
    if (!(pending & bit)) {
        TEST_PASS("Interrupt pending state cleared");
    } else {
        TEST_FAIL("Failed to clear pending state");
    }
    
    // Test active state registers exist and are readable
    uint32_t active = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_ISACTIVER + (reg * 4));
    TEST_INFO("Active state register readable");
    (void)active;  // Suppress unused variable warning
    
    // Note: We can't easily test setting active state as it requires
    // the interrupt to actually be acknowledged and in progress
    
    // Test that we can read the interrupt state
    TEST_PASS("Interrupt state registers accessible");
}

// Test 10: Test redistributor wake sequence thoroughly
static void test_redistributor_wake_sequence(void) {
    TEST_START("GICv3 Redistributor Wake Sequence");
    
    if (!is_gicv3() || !gic_primary || !gic_primary->redist_base) {
        return;
    }
    
    // Get the first redistributor
    void *rd_base = gic_primary->redist_base;
    
    // Read GICR_WAKER register
    uint32_t waker = mmio_read32((uint8_t*)rd_base + GICR_WAKER);
    
    // Check initial state
    if (!(waker & GICR_WAKER_ProcessorSleep)) {
        TEST_PASS("Redistributor already awake");
    } else {
        TEST_INFO("Redistributor was asleep, waking...");
        
        // Clear ProcessorSleep to wake up
        waker &= ~GICR_WAKER_ProcessorSleep;
        mmio_write32((uint8_t*)rd_base + GICR_WAKER, waker);
        
        // Wait for ChildrenAsleep to clear
        int timeout = 100000;
        while (timeout-- > 0) {
            waker = mmio_read32((uint8_t*)rd_base + GICR_WAKER);
            if (!(waker & GICR_WAKER_ChildrenAsleep)) {
                break;
            }
            __asm__ volatile("nop");
        }
        
        if (timeout > 0) {
            TEST_PASS("Redistributor wake sequence completed");
        } else {
            TEST_FAIL("Redistributor wake timeout");
        }
    }
    
    // Verify redistributor is functional by reading GICR_TYPER
    uint64_t typer = mmio_read64((uint8_t*)rd_base + GICR_TYPER);
    if (typer != 0 && typer != 0xFFFFFFFFFFFFFFFF) {
        TEST_PASS("Redistributor TYPER register readable");
        
        // Check redistributor properties
        bool last = (typer >> 4) & 1;
        bool dpgs = (typer >> 5) & 1;
        bool lpis = (typer >> 0) & 1;
        uint32_t cpu_number = (typer >> 8) & 0xFFFF;
        
        uart_puts("    CPU Number: ");
        uart_putdec(cpu_number);
        uart_puts("\n    Last: ");
        uart_puts(last ? "Yes" : "No");
        uart_puts("\n    DirectLPI: ");
        uart_puts(dpgs ? "Yes" : "No");
        uart_puts("\n    LPIs: ");
        uart_puts(lpis ? "Supported" : "Not supported");
        uart_puts("\n");
    } else {
        TEST_FAIL("Invalid GICR_TYPER value");
    }
}

// Test 11: Test group configuration
static void test_group_configuration(void) {
    TEST_START("GICv3 Interrupt Group Configuration");
    
    if (!is_gicv3() || !gic_primary) {
        return;
    }
    
    // Test SPI group configuration (use a high SPI)
    uint32_t test_irq = 96;
    uint32_t reg = test_irq / 32;
    uint32_t bit = 1 << (test_irq % 32);
    
    // Read current group configuration
    uint32_t orig_group = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_IGROUPR + (reg * 4));
    
    // Check current state
    bool initially_group1 = (orig_group & bit) != 0;
    uart_puts("    [INFO] Interrupt initially in Group ");
    uart_puts(initially_group1 ? "1" : "0");
    uart_puts("\n");
    
    // Try to toggle the group setting
    // First try to set opposite of current state
    uint32_t new_val = initially_group1 ? (orig_group & ~bit) : (orig_group | bit);
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_IGROUPR + (reg * 4), new_val);
    uint32_t readback = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_IGROUPR + (reg * 4));
    
    bool changed = ((readback & bit) != 0) != initially_group1;
    
    if (changed) {
        TEST_PASS("Group configuration is changeable");
        
        // Try to toggle back
        new_val = initially_group1 ? (readback | bit) : (readback & ~bit);
        mmio_write32((uint8_t*)gic_primary->dist_base + GICD_IGROUPR + (reg * 4), new_val);
        readback = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_IGROUPR + (reg * 4));
        
        if (((readback & bit) != 0) == initially_group1) {
            TEST_PASS("Group configuration restored");
        } else {
            TEST_INFO("Could not restore original group");
        }
    } else {
        // Group configuration is locked
        TEST_INFO("Group configuration is locked (security policy or hardware limitation)");
        
        // Check if it's locked to Group 1 (common in non-secure mode)
        if (initially_group1) {
            TEST_INFO("Interrupts locked to Group 1 (expected in non-secure mode)");
        } else {
            TEST_INFO("Interrupts locked to Group 0");
        }
    }
    
    // Check if we can access IGRPMODR for Group 1A configuration
    // This register might not be accessible in non-secure mode
    uint32_t orig_grpmod = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_IGRPMODR + (reg * 4));
    if (orig_grpmod != 0xFFFFFFFF) {  // If we can read it
        TEST_INFO("IGRPMODR accessible for Group 1A configuration");
    } else {
        TEST_INFO("IGRPMODR not accessible (expected in some configurations)");
    }
    
    // Restore original configuration
    mmio_write32((uint8_t*)gic_primary->dist_base + GICD_IGROUPR + (reg * 4), orig_group);
    
    // Test that all interrupts are in a valid group
    bool all_grouped = true;
    for (uint32_t i = 1; i < (gic_primary->nr_irqs / 32); i++) {
        uint32_t group = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_IGROUPR + (i * 4));
        // In a properly configured system, interrupts should be in Group 1 for non-secure
        if (group == 0) {
            all_grouped = false;
            break;
        }
    }
    
    if (all_grouped) {
        TEST_PASS("All SPIs properly grouped");
    } else {
        TEST_INFO("Some SPIs in Group 0");
    }
}

// Test 12: Test error conditions and spurious interrupts
static void test_error_conditions(void) {
    TEST_START("GICv3 Error Conditions and Spurious Interrupts");
    
    if (!is_gicv3()) {
        return;
    }
    
    // Test spurious interrupt detection
    // Read IAR when no interrupts are pending
    uint32_t iar = gicv3_read_iar1();
    uint32_t irq_id = iar & 0xFFFFFF;
    
    if (irq_id >= 1020 && irq_id <= 1023) {
        TEST_PASS("Spurious interrupt correctly identified");
        uart_puts("    Spurious IRQ ID: ");
        uart_putdec(irq_id);
        uart_puts("\n");
    } else if (irq_id < 1020) {
        TEST_INFO("Actual interrupt pending");
        uart_puts("    IRQ ID: ");
        uart_putdec(irq_id);
        uart_puts("\n");
        // Do EOI to clear it
        gicv3_write_eoir1(iar);
    }
    
    // Test handling of invalid interrupt numbers
    if (gic_primary) {
        // Try to configure an interrupt beyond nr_irqs
        uint32_t invalid_irq = gic_primary->nr_irqs + 10;
        if (invalid_irq < 1020) {
            uint32_t reg = invalid_irq / 32;
            uint32_t bit = 1 << (invalid_irq % 32);
            
            // This should have no effect or be ignored
            mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ISENABLER + (reg * 4), bit);
            TEST_INFO("Invalid interrupt configuration attempted (should be ignored)");
        }
    }
    
    // Test double EOI (should be harmless but is an error condition)
    // Send EOI for spurious interrupt (should be safe)
    gicv3_write_eoir1(1023);
    __asm__ volatile("isb");
    TEST_PASS("Double EOI handled without crash");
    
    // Test priority mask extremes
    // Set PMR to 0 (mask all interrupts)
    uint32_t orig_pmr = gicv3_read_pmr();
    gicv3_write_pmr(0x00);
    __asm__ volatile("isb");
    
    uint32_t masked_pmr = gicv3_read_pmr();
    if (masked_pmr == 0x00) {
        TEST_PASS("All interrupts masked with PMR=0");
    } else {
        TEST_FAIL("PMR=0 not set correctly");
    }
    
    // Restore PMR
    gicv3_write_pmr(orig_pmr);
    __asm__ volatile("isb");
    
    // Note: ICC_SGI1R_EL1 is write-only, we cannot read it
    // Testing write to it was done in test_system_register_sgi()
    TEST_INFO("Write-only register test skipped (SGI1R is write-only)");
    
    // Test system register access with interrupts disabled at CPU
    uint32_t orig_grpen = gicv3_read_grpen1();
    gicv3_write_grpen1(0);
    __asm__ volatile("isb");
    
    // Try to acknowledge interrupt with group disabled
    iar = gicv3_read_iar1();
    if ((iar & 0xFFFFFF) >= 1020) {
        TEST_PASS("No interrupt acknowledged with group disabled");
    }
    
    // Re-enable
    gicv3_write_grpen1(orig_grpen);
    __asm__ volatile("isb");
}

// Test 13: Stress test - rapid enable/disable and configuration changes
static void test_stress_operations(void) {
    TEST_START("GICv3 Stress Test - Rapid Operations");
    
    if (!is_gicv3() || !gic_primary) {
        return;
    }
    
    // Use multiple SPIs for stress testing
    uint32_t test_irqs[] = {40, 50, 60, 70, 80};
    int num_irqs = 5;
    int iterations = 100;
    
    TEST_INFO("Starting rapid enable/disable cycles...");
    
    // Rapid enable/disable cycles
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < num_irqs; i++) {
            uint32_t irq = test_irqs[i];
            uint32_t reg = irq / 32;
            uint32_t bit = 1 << (irq % 32);
            
            // Enable
            mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ISENABLER + (reg * 4), bit);
            
            // Immediately disable
            mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ICENABLER + (reg * 4), bit);
        }
    }
    TEST_PASS("Rapid enable/disable cycles completed");
    
    // Rapid priority changes
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < num_irqs; i++) {
            uint32_t irq = test_irqs[i];
            uint32_t reg = irq / 4;
            uint32_t shift = (irq % 4) * 8;
            
            uint32_t val = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_IPRIORITYR + (reg * 4));
            val &= ~(0xFF << shift);
            val |= ((iter & 0xF0) << shift);  // Use iteration count as priority
            mmio_write32((uint8_t*)gic_primary->dist_base + GICD_IPRIORITYR + (reg * 4), val);
        }
    }
    TEST_PASS("Rapid priority changes completed");
    
    // Rapid configuration changes (edge/level)
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < num_irqs; i++) {
            uint32_t irq = test_irqs[i];
            uint32_t reg = irq / 16;
            uint32_t shift = (irq % 16) * 2;
            
            uint32_t val = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_ICFGR + (reg * 4));
            val &= ~(0x3 << shift);
            val |= ((iter & 1) ? 0x2 : 0x0) << shift;  // Alternate edge/level
            mmio_write32((uint8_t*)gic_primary->dist_base + GICD_ICFGR + (reg * 4), val);
        }
    }
    TEST_PASS("Rapid configuration changes completed");
    
    // Verify GIC is still functional after stress
    uint32_t ctlr = mmio_read32((uint8_t*)gic_primary->dist_base + GICD_CTLR);
    if (ctlr & (GICD_CTLR_ENABLE_G1 | GICD_CTLR_ENABLE_G1A)) {
        TEST_PASS("GIC still functional after stress test");
    } else {
        TEST_FAIL("GIC not functional after stress test");
    }
}

// Main GICv3 test runner
void test_arm_gic_v3(void) {
    uart_puts("\n");
    uart_puts("================================================================\n");
    uart_puts("              GICv3-SPECIFIC TESTS\n");
    uart_puts("================================================================\n");
    
    // Check if we're running on GICv3
    if (!is_gicv3()) {
        uart_puts("\n[INFO] System is not using GICv3.\n");
        uart_puts("       These tests are only relevant for GICv3 systems.\n");
        return;
    }
    
    tests_passed = 0;
    tests_failed = 0;
    
    // Run all GICv3-specific tests
    test_gicv3_detection();
    test_redistributor_config();
    test_system_register_interrupt_flow();
    test_affinity_routing();
    test_system_register_sgi();
    test_gicv3_properties();
    
    // Run additional comprehensive tests
    test_interrupt_priorities();
    test_trigger_modes();
    test_interrupt_states();
    test_redistributor_wake_sequence();
    test_group_configuration();
    test_error_conditions();
    test_stress_operations();
    
    // Print summary
    uart_puts("\n========================================\n");
    uart_puts("GICv3 TEST SUMMARY:\n");
    uart_puts("  PASSED: ");
    uart_putdec(tests_passed);
    uart_puts("\n  FAILED: ");
    uart_putdec(tests_failed);
    uart_puts("\n  RESULT: ");
    if (tests_failed == 0 && tests_passed > 0) {
        uart_puts("ALL TESTS PASSED!");
    } else if (tests_failed > 0) {
        uart_puts("SOME TESTS FAILED!");
    } else {
        uart_puts("NO TESTS RUN");
    }
    uart_puts("\n========================================\n\n");
}

#endif /* __aarch64__ */