#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <uart.h>
#include <memory/mmu.h>
#include <memory/paging.h>
#include <memory/pmm.h>
#include <memory/memmap.h>
#include <exceptions/exceptions.h>
#include <tests/mmu_tests.h>
#include <tests/pmm_tests.h>
#include <tests/memory_tests.h>

void kernel_main(void* dtb) {
    
    uart_puts("\n=======================================\n");
    uart_puts("ARM64 Kernel Booting...\n");
    uart_puts("=======================================\n\n");
    
    uart_puts("Kernel entry point: kernel_main()\n");
    uart_puts("Device Tree Blob at: ");
    uart_puthex((uint64_t)dtb);
    uart_puts("\n");
    
    uart_puts("\nCurrent Exception Level: ");
    uint64_t current_el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r" (current_el));
    current_el = (current_el >> 2) & 0x3;
    uart_putc('0' + current_el);
    uart_puts("\n");
       
    uart_puts("\nSystem Information:\n");
    
    uart_puts("- Kernel loaded at: ");
    extern char __kernel_start;
    uart_puthex((uint64_t)&__kernel_start);
    uart_puts("\n");
    
    uart_puts("- Kernel ends at: ");
    extern char _kernel_end;
    uart_puthex((uint64_t)&_kernel_end);
    uart_puts("\n");
    
    // Initialize Memory Map Manager
    memmap_init();
    
    // Initialize Physical Memory Manager
    // Assume 256MB of RAM for now (we'll improve this later with device tree parsing)
    pmm_init((uint64_t)&_kernel_end, 256 * 1024 * 1024);
    
    
    // Run PMM tests
    run_pmm_tests();
    
    // Initialize exception handling
    exception_init();
    
    // Initialize and enable MMU
    mmu_init();
    enable_mmu();
    
    // Run MMU tests
    run_all_mmu_tests();
    
    // Run comprehensive memory tests
    run_comprehensive_memory_tests();
    
    uart_puts("\nKernel initialization complete!\n");
    uart_puts("System halted.\n");
    
    while (1) {
        __asm__ volatile("wfe");
    }
}
