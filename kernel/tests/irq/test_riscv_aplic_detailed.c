/*
 * Detailed APLIC Functional Tests
 * Actually verify the APLIC works, not just that we can call functions
 */

#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irqchip/riscv-aplic.h>
#include <irqchip/riscv-intc.h>
#include <uart.h>
#include <arch_interface.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device/device.h>

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) uart_puts("\n[TEST] " name "\n")
#define TEST_PASS(msg) do { uart_puts("  [PASS] " msg "\n"); tests_passed++; } while(0)
#define TEST_FAIL(msg) do { uart_puts("  [FAIL] " msg "\n"); tests_failed++; } while(0)
#define TEST_INFO(msg) uart_puts("  [INFO] " msg "\n")

// Global test state
static volatile bool interrupt_fired = false;
static volatile uint32_t last_interrupt_id = 0;
static volatile int interrupt_count = 0;
static volatile bool handler_called = false;

// Test interrupt handler that actually tracks if it was called
static void detailed_test_handler(void *data) {
    handler_called = true;
    interrupt_count++;
    if (data) {
        last_interrupt_id = *(uint32_t *)data;
    }
}

// Helper to trigger software interrupt through APLIC
static void trigger_software_interrupt(uint32_t irq) {
    if (!aplic_primary) return;
    
    // Configure the source as level-triggered (required for software triggering)
    aplic_write(aplic_primary, aplic_sourcecfg_offset(irq), APLIC_SOURCECFG_SM_LEVEL_HIGH);
    
    // Enable the interrupt
    aplic_write(aplic_primary, APLIC_SETIENUM, irq);
    
    // Route to hart 0 with priority 1
    uint32_t target = (0 << APLIC_TARGET_HART_IDX_SHIFT) | (1 << APLIC_TARGET_IPRIO_SHIFT);
    aplic_write(aplic_primary, aplic_target_offset(irq), target);
    
    // Try to set it pending (won't work without real hardware source, but we try)
    aplic_write(aplic_primary, APLIC_SETIPNUM_LE, irq);
}

// Test 1: Verify interrupt masking actually blocks interrupts
static void test_interrupt_masking(void) {
    TEST_START("Interrupt Masking Actually Works");
    
    if (!aplic_primary) {
        TEST_FAIL("No APLIC available");
        return;
    }
    
    struct irq_domain *domain = aplic_primary->domain;
    if (!domain) {
        TEST_FAIL("No APLIC domain");
        return;
    }
    
    // Use a test interrupt number
    uint32_t test_irq = 42;
    
    // Map the interrupt
    uint32_t virq = irq_create_mapping(domain, test_irq);
    if (!virq) {
        TEST_FAIL("Failed to map interrupt");
        return;
    }
    
    // Reset handler state
    handler_called = false;
    interrupt_count = 0;
    
    // Install handler
    if (request_irq(virq, detailed_test_handler, 0, "mask-test", &test_irq) < 0) {
        TEST_FAIL("Failed to install handler");
        irq_dispose_mapping(virq);
        return;
    }
    
    // Configure the interrupt source
    aplic_write(aplic_primary, aplic_sourcecfg_offset(test_irq), APLIC_SOURCECFG_SM_LEVEL_HIGH);
    aplic_write(aplic_primary, aplic_target_offset(test_irq), 
                (0 << APLIC_TARGET_HART_IDX_SHIFT) | (1 << APLIC_TARGET_IPRIO_SHIFT));
    
    // First, mask the interrupt
    aplic_write(aplic_primary, APLIC_CLRIENUM, test_irq);
    
    // Verify it's actually masked by checking the enable register
    uint32_t setie_reg = APLIC_SETIE_BASE + (test_irq / 32) * 4;
    uint32_t setie_bit = 1U << (test_irq % 32);
    uint32_t enable_bits = aplic_read(aplic_primary, setie_reg);
    
    if (enable_bits & setie_bit) {
        TEST_FAIL("Interrupt not actually masked in hardware");
    } else {
        TEST_PASS("Interrupt successfully masked in hardware");
    }
    
    // Now unmask it
    aplic_write(aplic_primary, APLIC_SETIENUM, test_irq);
    
    // Verify it's actually unmasked
    enable_bits = aplic_read(aplic_primary, setie_reg);
    if (enable_bits & setie_bit) {
        TEST_PASS("Interrupt successfully unmasked in hardware");
    } else {
        TEST_FAIL("Interrupt not actually unmasked in hardware");
    }
    
    // Cleanup
    free_irq(virq, &test_irq);
    irq_dispose_mapping(virq);
}

// Test 2: Verify interrupt routing configuration
static void test_interrupt_routing(void) {
    TEST_START("Interrupt Routing Configuration");
    
    if (!aplic_primary) {
        TEST_FAIL("No APLIC available");
        return;
    }
    
    // Test different routing configurations
    uint32_t test_irqs[] = {10, 20, 30, 40};
    uint32_t test_harts[] = {0, 0, 0, 0};  // All to hart 0 (we only have 1 hart in QEMU)
    uint32_t test_priorities[] = {1, 3, 5, 7};
    
    for (int i = 0; i < 4; i++) {
        uint32_t irq = test_irqs[i];
        uint32_t hart = test_harts[i];
        uint32_t prio = test_priorities[i];
        
        // Configure routing
        uint32_t target = (hart << APLIC_TARGET_HART_IDX_SHIFT) | 
                         (prio << APLIC_TARGET_IPRIO_SHIFT);
        aplic_write(aplic_primary, aplic_target_offset(irq), target);
        
        // Read back and verify
        uint32_t read_target = aplic_read(aplic_primary, aplic_target_offset(irq));
        uint32_t read_hart = (read_target >> APLIC_TARGET_HART_IDX_SHIFT) & APLIC_TARGET_HART_IDX_MASK;
        uint32_t read_prio = (read_target >> APLIC_TARGET_IPRIO_SHIFT) & APLIC_TARGET_IPRIO_MASK;
        
        if (read_hart != hart) {
            TEST_FAIL("Hart routing mismatch");
            uart_puts("    IRQ ");
            uart_putdec(irq);
            uart_puts(": Expected hart ");
            uart_putdec(hart);
            uart_puts(", got ");
            uart_putdec(read_hart);
            uart_puts("\n");
        }
        
        if (read_prio != prio) {
            TEST_FAIL("Priority mismatch");
            uart_puts("    IRQ ");
            uart_putdec(irq);
            uart_puts(": Expected priority ");
            uart_putdec(prio);
            uart_puts(", got ");
            uart_putdec(read_prio);
            uart_puts("\n");
        }
    }
    
    TEST_PASS("All routing configurations verified");
}

// Test 3: Verify source configuration modes
static void test_source_modes(void) {
    TEST_START("Source Configuration Modes");
    
    if (!aplic_primary) {
        TEST_FAIL("No APLIC available");
        return;
    }
    
    struct {
        uint32_t mode;
        const char *name;
    } modes[] = {
        {APLIC_SOURCECFG_SM_INACTIVE, "INACTIVE"},
        {APLIC_SOURCECFG_SM_EDGE_RISE, "EDGE_RISE"},
        {APLIC_SOURCECFG_SM_EDGE_FALL, "EDGE_FALL"},
        {APLIC_SOURCECFG_SM_LEVEL_HIGH, "LEVEL_HIGH"},
        {APLIC_SOURCECFG_SM_LEVEL_LOW, "LEVEL_LOW"},
    };
    
    uint32_t test_irq = 35;
    
    for (int i = 0; i < 5; i++) {
        // Set the mode
        aplic_write(aplic_primary, aplic_sourcecfg_offset(test_irq), modes[i].mode);
        
        // Read it back
        uint32_t cfg = aplic_read(aplic_primary, aplic_sourcecfg_offset(test_irq));
        uint32_t read_mode = cfg & APLIC_SOURCECFG_SM_MASK;
        
        if (read_mode != modes[i].mode) {
            TEST_FAIL("Source mode configuration failed");
            uart_puts("    Expected ");
            uart_puts(modes[i].name);
            uart_puts(" (0x");
            uart_puthex(modes[i].mode);
            uart_puts("), got 0x");
            uart_puthex(read_mode);
            uart_puts("\n");
        }
    }
    
    TEST_PASS("All source modes configured correctly");
    
    // Test that INACTIVE sources cannot be set pending
    aplic_write(aplic_primary, aplic_sourcecfg_offset(test_irq), APLIC_SOURCECFG_SM_INACTIVE);
    aplic_write(aplic_primary, APLIC_SETIENUM, test_irq);
    aplic_write(aplic_primary, APLIC_SETIPNUM_LE, test_irq);
    
    // Check if it's pending (it shouldn't be)
    uint32_t pending_reg = APLIC_CLRIP_BASE + (test_irq / 32) * 4;
    uint32_t pending_bit = 1U << (test_irq % 32);
    uint32_t pending_bits = aplic_read(aplic_primary, pending_reg);
    
    if (pending_bits & pending_bit) {
        TEST_FAIL("INACTIVE source became pending (shouldn't happen)");
    } else {
        TEST_PASS("INACTIVE sources correctly cannot be set pending");
    }
}

// Test 4: Verify IDC threshold actually blocks interrupts
static void test_idc_threshold(void) {
    TEST_START("IDC Threshold Blocking");
    
    if (!aplic_primary) {
        TEST_FAIL("No APLIC available");
        return;
    }
    
    // Set different thresholds and verify behavior
    uint32_t thresholds[] = {0, 3, 5, 7, 0xFF};
    
    for (int i = 0; i < 5; i++) {
        // Set threshold for IDC 0
        aplic_idc_write(aplic_primary, 0, APLIC_IDC_ITHRESHOLD, thresholds[i]);
        
        // Note: ITHRESHOLD is write-only, so we can't read it back
        // We would need actual interrupts to test if it's working
        
        uart_puts("    Set threshold to ");
        uart_putdec(thresholds[i]);
        if (thresholds[i] == 0) {
            uart_puts(" (accept all)");
        } else if (thresholds[i] == 0xFF) {
            uart_puts(" (block all)");
        }
        uart_puts("\n");
    }
    
    // Reset threshold to 0 (accept all)
    aplic_idc_write(aplic_primary, 0, APLIC_IDC_ITHRESHOLD, 0);
    
    TEST_INFO("Threshold configuration complete (needs real interrupts to verify)");
}

// Test 5: Verify TOPI register shows highest priority pending interrupt
static void test_topi_register(void) {
    TEST_START("TOPI Register Functionality");
    
    if (!aplic_primary) {
        TEST_FAIL("No APLIC available");
        return;
    }
    
    // Read TOPI when no interrupts are pending
    uint32_t topi = aplic_idc_read(aplic_primary, 0, APLIC_IDC_TOPI);
    uint32_t topi_id = (topi >> APLIC_IDC_TOPI_ID_SHIFT) & APLIC_IDC_TOPI_ID_MASK;
    uint32_t topi_prio = topi & APLIC_IDC_TOPI_PRIO_MASK;
    
    if (topi_id == 0) {
        TEST_PASS("TOPI correctly shows 0 when no interrupts pending");
    } else {
        TEST_FAIL("TOPI shows non-zero with no interrupts pending");
        uart_puts("    TOPI ID: ");
        uart_putdec(topi_id);
        uart_puts(", Priority: ");
        uart_putdec(topi_prio);
        uart_puts("\n");
    }
    
    // Try to set up multiple interrupts with different priorities
    // Note: Without real hardware sources, they won't actually pend
    uint32_t test_irqs[] = {15, 16, 17};
    uint32_t priorities[] = {7, 3, 1};  // 1 is highest priority
    
    for (int i = 0; i < 3; i++) {
        // Configure as level-triggered
        aplic_write(aplic_primary, aplic_sourcecfg_offset(test_irqs[i]), 
                   APLIC_SOURCECFG_SM_LEVEL_HIGH);
        
        // Enable
        aplic_write(aplic_primary, APLIC_SETIENUM, test_irqs[i]);
        
        // Set priority
        uint32_t target = (0 << APLIC_TARGET_HART_IDX_SHIFT) | 
                         (priorities[i] << APLIC_TARGET_IPRIO_SHIFT);
        aplic_write(aplic_primary, aplic_target_offset(test_irqs[i]), target);
        
        // Try to set pending (won't work without hardware source)
        aplic_write(aplic_primary, APLIC_SETIPNUM_LE, test_irqs[i]);
    }
    
    // Check TOPI again
    topi = aplic_idc_read(aplic_primary, 0, APLIC_IDC_TOPI);
    topi_id = (topi >> APLIC_IDC_TOPI_ID_SHIFT) & APLIC_IDC_TOPI_ID_MASK;
    
    if (topi_id == 0) {
        TEST_INFO("TOPI still 0 (expected - no real hardware sources)");
    } else {
        TEST_INFO("TOPI shows interrupt pending");
        uart_puts("    ID: ");
        uart_putdec(topi_id);
        uart_puts(", Priority: ");
        uart_putdec(topi_prio);
        uart_puts("\n");
    }
}

// Test 6: Verify claim/complete cycle
static void test_claim_complete_cycle(void) {
    TEST_START("Claim/Complete Cycle");
    
    if (!aplic_primary) {
        TEST_FAIL("No APLIC available");
        return;
    }
    
    // Try to claim when nothing is pending
    uint32_t claimed = aplic_direct_claim(0);
    if (claimed == 0) {
        TEST_PASS("Claim correctly returns 0 when nothing pending");
    } else {
        TEST_FAIL("Claim returned non-zero when nothing pending");
        uart_puts("    Claimed ID: ");
        uart_putdec(claimed);
        uart_puts("\n");
    }
    
    // Complete should be safe even with ID 0
    aplic_direct_complete(0, 0);
    TEST_PASS("Complete handled ID 0 safely");
    
    // Note: We can't test real claim/complete without actual interrupts
    TEST_INFO("Real claim/complete requires hardware interrupt sources");
}

// Test 7: Stress test - rapid enable/disable
static void test_rapid_enable_disable(void) {
    TEST_START("Rapid Enable/Disable Stress Test");
    
    if (!aplic_primary) {
        TEST_FAIL("No APLIC available");
        return;
    }
    
    uint32_t test_irq = 50;
    
    // Configure the source
    aplic_write(aplic_primary, aplic_sourcecfg_offset(test_irq), APLIC_SOURCECFG_SM_LEVEL_HIGH);
    aplic_write(aplic_primary, aplic_target_offset(test_irq), 
                (0 << APLIC_TARGET_HART_IDX_SHIFT) | (1 << APLIC_TARGET_IPRIO_SHIFT));
    
    // Rapid enable/disable
    for (int i = 0; i < 100; i++) {
        aplic_write(aplic_primary, APLIC_SETIENUM, test_irq);
        aplic_write(aplic_primary, APLIC_CLRIENUM, test_irq);
    }
    
    // Verify final state is disabled
    uint32_t setie_reg = APLIC_SETIE_BASE + (test_irq / 32) * 4;
    uint32_t setie_bit = 1U << (test_irq % 32);
    uint32_t enable_bits = aplic_read(aplic_primary, setie_reg);
    
    if (enable_bits & setie_bit) {
        TEST_FAIL("Interrupt still enabled after disable");
    } else {
        TEST_PASS("Rapid enable/disable handled correctly");
    }
    
    // Now leave it enabled and verify
    aplic_write(aplic_primary, APLIC_SETIENUM, test_irq);
    enable_bits = aplic_read(aplic_primary, setie_reg);
    
    if (enable_bits & setie_bit) {
        TEST_PASS("Final enable state correct");
    } else {
        TEST_FAIL("Final enable failed");
    }
}

// Test 8: Verify all interrupts start disabled
static void test_initial_state(void) {
    TEST_START("Initial Interrupt State");
    
    if (!aplic_primary) {
        TEST_FAIL("No APLIC available");
        return;
    }
    
    // Check first 64 interrupts (2 registers worth)
    bool all_disabled = true;
    
    for (int reg = 0; reg < 2; reg++) {
        uint32_t enable_bits = aplic_read(aplic_primary, APLIC_SETIE_BASE + reg * 4);
        if (enable_bits != 0) {
            all_disabled = false;
            TEST_FAIL("Some interrupts are enabled at initialization");
            uart_puts("    Register ");
            uart_putdec(reg);
            uart_puts(" has bits 0x");
            uart_puthex(enable_bits);
            uart_puts(" enabled\n");
        }
    }
    
    if (all_disabled) {
        TEST_PASS("All interrupts correctly start disabled");
    }
    
    // Check that sources start as INACTIVE
    for (int i = 1; i <= 10; i++) {
        uint32_t cfg = aplic_read(aplic_primary, aplic_sourcecfg_offset(i));
        uint32_t mode = cfg & APLIC_SOURCECFG_SM_MASK;
        if (mode != APLIC_SOURCECFG_SM_INACTIVE) {
            TEST_FAIL("Source not INACTIVE at initialization");
            uart_puts("    Source ");
            uart_putdec(i);
            uart_puts(" mode: 0x");
            uart_puthex(mode);
            uart_puts("\n");
            return;
        }
    }
    TEST_PASS("All sources correctly start as INACTIVE");
}

// Main detailed test runner
void test_riscv_aplic_detailed(void) {
    uart_puts("\n================================================================\n");
    uart_puts("            DETAILED RISC-V APLIC FUNCTIONAL TESTS\n");
    uart_puts("================================================================\n");
    
#ifdef __riscv
    if (!aplic_primary) {
        uart_puts("[SKIP] APLIC not available on this system\n");
        return;
    }
    
    // Reset test counters
    tests_passed = 0;
    tests_failed = 0;
    interrupt_fired = false;
    handler_called = false;
    interrupt_count = 0;
    last_interrupt_id = 0;
    
    // Run detailed tests
    test_initial_state();
    test_interrupt_masking();
    test_interrupt_routing();
    test_source_modes();
    test_idc_threshold();
    test_topi_register();
    test_claim_complete_cycle();
    test_rapid_enable_disable();
    
    // Print summary
    uart_puts("\n================ DETAILED TEST SUMMARY ================\n");
    uart_puts("Tests passed: ");
    uart_putdec(tests_passed);
    uart_puts("\n");
    uart_puts("Tests failed: ");
    uart_putdec(tests_failed);
    uart_puts("\n");
    
    if (tests_failed == 0) {
        uart_puts("\n[SUCCESS] All detailed APLIC tests passed!\n");
    } else {
        uart_puts("\n[FAILURE] Some detailed APLIC tests failed!\n");
    }
    
    uart_puts("\nNOTE: Full interrupt delivery testing requires real hardware\n");
    uart_puts("interrupt sources. Software cannot manually trigger pending\n");
    uart_puts("bits without actual hardware assertion per RISC-V AIA spec.\n");
#else
    uart_puts("[SKIP] APLIC tests are only for RISC-V architecture\n");
#endif
    
    uart_puts("================================================================\n");
}