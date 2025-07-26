#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <uart.h>
#include <memory/mmu.h>
#include <memory/paging.h>
#include <memory/pmm.h>
#include <memory/memmap.h>
#include <exceptions/exceptions.h>
// #include <tests/mmu_tests.h>
// #include <tests/pmm_tests.h>
// #include <tests/memory_tests.h>
// #include <tests/fdt_tests.h>
#include <drivers/fdt.h>
#include <memory/vmm.h>

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
    
    // Parse Device Tree to get memory information
    memory_info_t mem_info;
    uart_puts("\nParsing Device Tree...\n");
    
    if (!fdt_get_memory(dtb, &mem_info)) {
        uart_puts("WARNING: Failed to parse memory from DTB!\n");
        uart_puts("Using default: 256MB at 0x40000000\n");
        mem_info.count = 1;
        mem_info.regions[0].base = 0x40000000;
        mem_info.regions[0].size = 256 * 1024 * 1024;
        mem_info.total_size = 256 * 1024 * 1024;
    } else {
        uart_puts("Successfully parsed memory from DTB\n");
    }
    
    // Print memory information
    fdt_print_memory_info(&mem_info);
    
    // Initialize Memory Map Manager
    memmap_init();
    
    // Initialize Physical Memory Manager with actual memory size
    // For now, use the first region's size (assuming contiguous memory from 0x40000000)
    pmm_init((uint64_t)&_kernel_end, mem_info.regions[0].size);
    
    // Initialize Virtual Memory Manager
    vmm_init();
    
    // Debug: Check what's in the page tables
    vmm_debug_walk(vmm_get_kernel_context(), 0xFFFF000040200000UL);  // Kernel address
    vmm_debug_walk(vmm_get_kernel_context(), 0xFFFFA00000000000UL);  // DMAP start
    
    // Create DMAP region for all physical memory
    vmm_create_dmap();
    
    // FDT tests have been verified to work correctly
    // run_fdt_tests();
    
    // TODO: Update these tests for higher-half kernel
    // The old tests assume physical addresses and need updating:
    // - PMM tests need to account for virtual addresses
    // - MMU tests need rewriting since MMU is already enabled
    // - Memory tests need updating for new memory layout
    
    uart_puts("\nKernel initialization complete!\n");
    uart_puts("System halted.\n");
    
    while (1) {
        __asm__ volatile("wfe");
    }
}
