#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <uart.h>
#include <panic.h>
#include <memory/pmm.h>
#include <memory/memmap.h>
#include <exceptions/exceptions.h>
#include <drivers/fdt.h>
#include <drivers/fdt_mgr.h>
#include <memory/vmm.h>
#include <platform/devmap.h>
#include <device/device.h>
// #include <tests/mmu_tests.h>
// #include <tests/pmm_tests.h>
// #include <tests/memory_tests.h>
// #include <tests/fdt_tests.h>
// #include <tests/fdt_mgr_tests.h>
// #include <tests/test_dmap.h>

// External symbols from linker script
extern char __kernel_start;
extern char _kernel_end;

void kernel_main(void* dtb) {

    // Initialize FDT Manager (just preserves pointer) 
    if (!fdt_mgr_init(dtb)) {
        // Cannot output warning - UART not available yet
    }
    
    // Initialize memory subsystems 
    memmap_init();
    
    // Parse Device Tree to get memory information using FDT manager
    memory_info_t mem_info;
    if (!fdt_mgr_get_memory_info(&mem_info)) {
        panic("Failed to parse memory information from FDT");
    }
    
    // Initialize PMM
    pmm_init((uint64_t)&_kernel_end, (struct memory_info *)&mem_info);
    
    // Reserve FDT pages in PMM before any allocations
    // Keep this separate from pmm_init for clean separation of concerns
    if (!fdt_mgr_reserve_pages()) {
        panic("Failed to reserve FDT pages in PMM");
    }
    
    // Initialize VMM
    vmm_init();
    
    // Create DMAP region for all physical memory
    // This must be done before device mappings so PMM can use DMAP for page clearing
    vmm_create_dmap((struct memory_info *)&mem_info);
    
    // Map FDT to permanent virtual address
    if (!fdt_mgr_map_virtual()) {
        // No UART output for this panic 
        panic("Failed to map FDT to virtual memory");
    }
    
    // Initialize device subsystem (pool, tree parser, enumeration)
    int device_count = device_init(fdt_mgr_get_blob());
    if (device_count < 0) {
        // No UART output for this panic 
        panic("Failed to initialize device subsystem");
    }
    
    // Initialize Device Mapping system
    // Now devmap_init can use the discovered devices
    devmap_init();
    
    // Initialize UART with proper device mapping
    uart_init();
   
    // KERNEL LOGS START HERE! 
    uart_puts("\n=======================================\n");
    uart_puts("ARM64 Kernel Booting...\n");
    uart_puts("=======================================\n\n");
    
    uart_puts("Kernel entry point: kernel_main()\n");
    uart_puts("Device Tree Blob at: ");
    uart_puthex((uint64_t)dtb);
    uart_puts(" (");
    if (dtb) {
        uint32_t *magic_ptr = (uint32_t *)dtb;
        uart_puts("magic=");
        uart_puthex(*magic_ptr);
    } else {
        uart_puts("NULL");
    }
    uart_puts(")\n");
    
    uart_puts("\nCurrent Exception Level: ");
    uint64_t current_el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r" (current_el));
    current_el = (current_el >> 2) & 0x3;
    uart_putc('0' + current_el);
    uart_puts("\n");
       
    uart_puts("\nSystem Information:\n");
    
    uart_puts("- Kernel loaded at: ");
    uart_puthex((uint64_t)&__kernel_start);
    uart_puts("\n");
    
    uart_puts("- Kernel ends at: ");
    uart_puthex((uint64_t)&_kernel_end);
    uart_puts("\n");
    
    // Report memory information that was parsed earlier
    uart_puts("\nMemory Configuration:\n");
    fdt_print_memory_info(&mem_info);
    
    // Report FDT manager state
    fdt_mgr_print_info();
    
    // Debug: Check what's in the page tables
    vmm_debug_walk(vmm_get_kernel_context(), 0xFFFF000040200000UL);  // Kernel address
    vmm_debug_walk(vmm_get_kernel_context(), 0xFFFFA00000000000UL);  // DMAP start
    
    // Print device mappings
    devmap_print_mappings();
    
    // Print memory statistics
    pmm_print_stats();
    
    // Print device pool statistics
    // extern void device_pool_print_stats(void);
    // device_pool_print_stats();
   
    // KERNEL TESTS START HERE!

    // No tests in here for now. 
    
    uart_puts("\nKernel initialization complete!\n");
    uart_puts("System halted.\n");
    
    while (1) {
        __asm__ volatile("wfe");
    }
}
