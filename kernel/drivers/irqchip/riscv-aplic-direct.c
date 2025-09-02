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
        // Set delivery mode to 1 (deliver interrupts)
        aplic_idc_write(aplic, i, APLIC_IDC_IDELIVERY, 1);
        
        // Set threshold to 0 (accept all priorities)
        aplic_idc_write(aplic, i, APLIC_IDC_ITHRESHOLD, 0);
        
        uart_puts("APLIC-Direct: Initialized IDC ");
        uart_putdec(i);
        uart_puts("\n");
    }
    
    // Configure default target for all interrupts (hart 0 for now)
    for (i = 1; i <= aplic->nr_sources; i++) {
        uint32_t target = 0;  // Hart 0
        target |= (APLIC_DEFAULT_PRIORITY << APLIC_TARGET_IPRIO_SHIFT);
        aplic_write(aplic, aplic_target_offset(i), target);
    }
    
    uart_puts("APLIC-Direct: Direct mode initialization complete\n");
    return 0;
}

// Handle APLIC interrupt in direct mode
void aplic_direct_handle_irq(void) {
    struct aplic_data *aplic = aplic_primary;
    uint32_t idc = 0;  // TODO: Get actual IDC for current hart
    uint32_t irq;
    
    if (!aplic) {
        uart_puts("APLIC-Direct: No APLIC instance\n");
        return;
    }
    
    // Claim interrupt from IDC
    irq = aplic_direct_claim(idc);
    
    if (irq && irq <= aplic->nr_sources) {
        // Convert hardware IRQ to virtual IRQ and handle
        uint32_t virq = irq_find_mapping(aplic->domain, irq);
        if (virq) {
            generic_handle_irq(virq);
        } else {
            uart_puts("APLIC-Direct: No mapping for hwirq ");
            uart_putdec(irq);
            uart_puts("\n");
        }
        
        // Complete the interrupt
        aplic_direct_complete(idc, irq);
    }
}

// Claim an interrupt from IDC
uint32_t aplic_direct_claim(uint32_t idc) {
    struct aplic_data *aplic = aplic_primary;
    uint32_t topi;
    
    if (!aplic) return 0;
    
    // Read TOPI to get highest priority pending interrupt
    topi = aplic_idc_read(aplic, idc, APLIC_IDC_TOPI);
    
    // Extract interrupt ID from TOPI
    uint32_t irq = (topi >> APLIC_IDC_TOPI_ID_SHIFT) & APLIC_IDC_TOPI_ID_MASK;
    
    if (irq) {
        // Claim the interrupt by reading CLAIMI
        aplic_idc_read(aplic, idc, APLIC_IDC_CLAIMI);
    }
    
    return irq;
}

// Complete/acknowledge an interrupt
void aplic_direct_complete(uint32_t idc, uint32_t irq) {
    struct aplic_data *aplic = aplic_primary;
    
    if (!aplic || !irq) return;
    
    // Write IRQ number to CLAIMI to complete
    aplic_idc_write(aplic, idc, APLIC_IDC_CLAIMI, irq);
}

#endif // __riscv