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
// #include <tests/fdt_mgr_tests.h>
#include <drivers/fdt.h>
#include <drivers/fdt_mgr.h>
#include <memory/vmm.h>
#include <platform/devmap.h>
#include <device/device.h>
#include <device/resource.h>
#include <device/device_tree.h>

// External symbols from linker script
extern char __kernel_start;
extern char _kernel_end;

void kernel_main(void* dtb) {
    // Initialize UART with temporary boot.S mapping
    uart_init();
    // uart_puts("\nKernel starting...\n");
    
    // Initialize FDT manager early
    if (!fdt_mgr_init(dtb)) {
        uart_puts("WARNING: Failed to initialize FDT manager\n");
    }
    
    // Initialize memory subsystems first
    // uart_puts("Initializing memmap...\n");
    memmap_init();
    
    // Parse Device Tree to get memory information using FDT manager
    memory_info_t mem_info;
    if (!fdt_mgr_get_memory_info(&mem_info)) {
        // Use defaults if FDT parse fails
        uart_puts("WARNING: Failed to parse memory from FDT, using defaults\n");
        mem_info.count = 1;
        mem_info.regions[0].base = 0x40000000;
        mem_info.regions[0].size = 256 * 1024 * 1024;
        mem_info.total_size = 256 * 1024 * 1024;
    }
    
    // Initialize PMM
    // uart_puts("Initializing PMM...\n");
    pmm_init((uint64_t)&_kernel_end, mem_info.regions[0].size);
    
    // Reserve FDT pages in PMM before any allocations
    if (!fdt_mgr_reserve_pages()) {
        uart_puts("WARNING: Failed to reserve FDT pages\n");
    }
    
    // Initialize VMM
    // uart_puts("Initializing VMM...\n");
    vmm_init();
    
    // Create DMAP region for all physical memory
    // This must be done before device mappings so PMM can use DMAP for page clearing
    // uart_puts("Creating DMAP...\n");
    vmm_create_dmap();
    
    // Map FDT to permanent virtual address
    if (!fdt_mgr_map_virtual()) {
        uart_puts("WARNING: Failed to map FDT to virtual memory\n");
    }
    
    // Initialize device tree subsystem BEFORE devmap_init
    // This allows devmap to use discovered devices
    uart_puts("\nInitializing device management...\n");
    if (device_tree_init(fdt_mgr_get_blob()) < 0) {
        uart_puts("ERROR: Failed to initialize device tree subsystem\n");
    } else {
        // Enumerate all devices from FDT
        uart_puts("Enumerating devices from device tree...\n");
        int device_count = device_tree_populate_devices();
        if (device_count < 0) {
            uart_puts("ERROR: Failed to enumerate devices\n");
        } else {
            uart_puts("Device enumeration complete. Found ");
            uart_puthex(device_count);
            uart_puts(" devices.\n");
            
            // Migrate devices to permanent storage now that PMM is available
            // This frees up the early pool and provides more space for devices
            if (device_count > 0) {
                int migrated = device_migrate_to_permanent();
                if (migrated > 0) {
                    uart_puts("Migrated ");
                    uart_puthex(migrated);
                    uart_puts(" devices to permanent storage\n");
                }
            }
        }
    }
    
    // Initialize Device Mapping system
    // Now devmap_init can use the discovered devices
    // uart_puts("Initializing devmap...\n");
    devmap_init();
    
    // Now initialize and update UART with proper mapping
    uart_init();
    uart_update_base();
   
    // Start outputting kernel boot messages 
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
    
    uart_puts("\nDevice mappings initialized successfully\n");
    
    // Debug: Check what's in the page tables
    vmm_debug_walk(vmm_get_kernel_context(), 0xFFFF000040200000UL);  // Kernel address
    vmm_debug_walk(vmm_get_kernel_context(), 0xFFFFA00000000000UL);  // DMAP start
    
    // Print device mappings
    devmap_print_mappings();
    
    // Print memory statistics
    pmm_print_stats();
    
    // Print device tree that was enumerated earlier
    device_tree_dump_devices();
    
    // Commented out detailed device info - not needed for normal boot
    // struct device *uart_dev = device_find_by_compatible("arm,pl011");
    // struct device *gic_dev = device_find_by_type(DEV_TYPE_INTERRUPT);
    
    // Print early pool statistics
    extern void early_pool_print_stats(void);
    early_pool_print_stats();
    
    // Run device tests
    extern void run_device_tests(void);
    run_device_tests();
    
    // Run FDT manager tests
    // run_fdt_mgr_tests();
    
    // FDT tests work correctly
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
