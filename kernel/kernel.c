#include <stdint.h>
#include <stddef.h>
#include <uart.h>
#include <memory/mmu.h>
#include <exceptions.h>

// Global debug flags for GDB inspection
volatile uint32_t mmu_enabled_flag = 0;
volatile uint32_t kernel_running_flag = 0;
volatile uint32_t debug_counter = 0;

void kernel_main(void* dtb) {
    kernel_running_flag = 0xCAFEBABE;
    
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
    
    // Initialize exception handling
    exception_init();
    
    // Initialize and enable MMU
    mmu_init();
    enable_mmu();
    
    mmu_enabled_flag = 0xDEADBEEF;
    
    uart_puts("\nTesting UART after MMU enable...\n");
    uart_puts("If you see this, MMU is working correctly!\n");
    
    // Test exception handling (comment out for normal operation)
    // uart_puts("\nTesting exception handling with page fault...\n");
    // uart_puts("Attempting to access unmapped address 0x1000...\n");
    // volatile uint32_t *unmapped = (volatile uint32_t *)0x1000;
    // uint32_t value = *unmapped;  // This should trigger a data abort
    // uart_puts("If you see this, page fault handling failed!\n");
    // uart_puthex(value);
    // uart_puts("\n");
    
    uart_puts("\nKernel initialization complete!\n");
    uart_puts("System halted.\n");
    
    while (1) {
        debug_counter++;
        __asm__ volatile("wfe");
    }
}
