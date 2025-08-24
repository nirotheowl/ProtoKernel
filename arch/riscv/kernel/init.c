/*
 * arch/riscv/kernel/init.c
 * 
 * RISC-V C entry and initialization
 */

#include <stdint.h>
#include <stddef.h>
#include <arch_cache.h>
#include <arch_exceptions.h>
#include <exceptions/exceptions.h>

// External symbols from linker script
extern char _kernel_end;

// External from boot.S - physical base address storage
extern uint64_t phys_base_storage;

// Global kernel physical base (used by VIRT_TO_PHYS/PHYS_TO_VIRT macros)
uint64_t kernel_phys_base;

// Forward declaration for kernel_main
void kernel_main(void* dtb);


// RISC-V architecture entry point from boot.S
void init_riscv(unsigned long hart_id, void *dtb) {
    /*
     * At this point:
     * - We're running in supervisor mode
     * - Stack is set up
     * - BSS is cleared
     * - DTB pointer is passed from bootloader/OpenSBI
     * - Hart ID indicates which CPU core we're on
     */
    
    // Initialize kernel physical base from boot.S
    // phys_base_storage was set in physical memory during boot
    kernel_phys_base = phys_base_storage;
    
    
    // Initialize cache subsystem
    arch_cache_init();
    
    // Install trap/exception handlers using the architecture-agnostic function
    exception_init();
    
    // Jump to common kernel initialization
    kernel_main(dtb);
    
    // Should not return, but halt if it does
    while (1) {
        __asm__ volatile("wfi");
    }
}
