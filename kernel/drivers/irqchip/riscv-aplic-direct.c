/*
 * kernel/drivers/irqchip/riscv-aplic-direct.c
 * 
 * RISC-V APLIC Direct Mode Implementation
 */

// Only compile this driver for RISC-V architecture
#ifdef __riscv

#include <irqchip/riscv-aplic.h>
#include <irq/irq.h>
#include <uart.h>
#include <arch_io.h>
#include <stddef.h>

// Initialize APLIC in direct mode
int aplic_direct_init(struct aplic_data *aplic) {
    uint32_t i;
    
    uart_puts("APLIC-Direct: Initializing direct mode\n");
    
    // Initialize IDCs (Interrupt Delivery Controllers)
    for (i = 0; i < aplic->nr_idcs; i++) {
        // Calculate IDC base for debugging
        void *idc_base = aplic_idc_base(aplic, i);
        uart_puts("APLIC-Direct: IDC ");
        uart_putdec(i);
        uart_puts(" at offset 0x");
        uart_puthex((uint64_t)idc_base - (uint64_t)aplic->base);
        uart_puts("\n");
        
        // Set delivery mode to 1 (deliver interrupts)
        aplic_idc_write(aplic, i, APLIC_IDC_IDELIVERY, 1);
        
        // Set threshold to 0 (accept all priorities)
        aplic_idc_write(aplic, i, APLIC_IDC_ITHRESHOLD, 0);
        
        // Try reading TOPI to see if IDC is accessible
        uint32_t topi = aplic_idc_read(aplic, i, APLIC_IDC_TOPI);
        uart_puts("APLIC-Direct: IDC ");
        uart_putdec(i);
        uart_puts(" TOPI=0x");
        uart_puthex(topi);
        uart_puts(" (should be 0 initially)\n");
    }
    
    // Configure default target for all interrupts
    // In direct mode, target register format:
    // Bits [31:18]: Hart index
    // Bits [17:12]: Guest index (0 for no virtualization)
    // Bits [7:0]: Priority
    for (i = 1; i <= aplic->nr_sources; i++) {
        uint32_t target = 0;
        // Route to hart 0 by default
        target |= (0 << APLIC_TARGET_HART_IDX_SHIFT) & (APLIC_TARGET_HART_IDX_MASK << APLIC_TARGET_HART_IDX_SHIFT);
        // Set default priority
        target |= (APLIC_DEFAULT_PRIORITY << APLIC_TARGET_IPRIO_SHIFT) & (APLIC_TARGET_IPRIO_MASK << APLIC_TARGET_IPRIO_SHIFT);
        aplic_write(aplic, aplic_target_offset(i), target);
    }
    
    uart_puts("APLIC-Direct: Direct mode initialization complete\n");
    return 0;
}

// Handle APLIC interrupt in direct mode
void aplic_direct_handle_irq(void) {
    struct aplic_data *aplic = aplic_primary;
    uint32_t idc = 0;  // TODO: Get actual IDC for current hart
    uint32_t claimi;
    uint32_t irq;
    
    if (!aplic) {
        uart_puts("APLIC-Direct: No APLIC instance\n");
        return;
    }
    
    // Keep claiming and handling interrupts until no more pending
    // This is important as multiple interrupts may be pending
    while (1) {
        // Read CLAIMI register which returns the highest priority pending interrupt
        // and automatically clears the pending bit
        claimi = aplic_idc_read(aplic, idc, APLIC_IDC_CLAIMI);
        
        // Extract interrupt ID from CLAIMI (bits [25:16])
        irq = (claimi >> APLIC_IDC_TOPI_ID_SHIFT) & APLIC_IDC_TOPI_ID_MASK;
        
        // If no interrupt pending, we're done
        if (irq == 0) {
            break;
        }
        
        // Validate IRQ number
        if (irq > aplic->nr_sources) {
            uart_puts("APLIC-Direct: Invalid IRQ ");
            uart_putdec(irq);
            uart_puts("\n");
            break;
        }
        
        // Convert hardware IRQ to virtual IRQ and handle
        uint32_t virq = irq_find_mapping(aplic->domain, irq);
        if (virq) {
            generic_handle_irq(virq);
        } else {
            uart_puts("APLIC-Direct: No mapping for hwirq ");
            uart_putdec(irq);
            uart_puts("\n");
        }
    }
}

// Claim an interrupt from IDC
// Note: In APLIC, claiming is done by reading CLAIMI which automatically
// clears the pending bit. The new implementation handles this in the
// main interrupt handler loop.
uint32_t aplic_direct_claim(uint32_t idc) {
    struct aplic_data *aplic = aplic_primary;
    uint32_t claimi;
    
    if (!aplic) return 0;
    
    // Read CLAIMI register which returns interrupt ID and clears pending
    claimi = aplic_idc_read(aplic, idc, APLIC_IDC_CLAIMI);
    
    // Extract interrupt ID from CLAIMI
    return (claimi >> APLIC_IDC_TOPI_ID_SHIFT) & APLIC_IDC_TOPI_ID_MASK;
}

// Complete/acknowledge an interrupt
// Note: In APLIC direct mode, there's no explicit completion step
// The interrupt is completed when it's claimed via CLAIMI
void aplic_direct_complete(uint32_t idc, uint32_t irq) {
    // In direct mode, completion is automatic when claiming
    // This function is kept for API compatibility
    (void)idc;
    (void)irq;
}

#endif // __riscv