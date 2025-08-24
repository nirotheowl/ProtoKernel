/*
 * arch/arm64/kernel/init.c
 *
 * ARM64 C entry and initialization
 */

#include <stdint.h>
#include <stddef.h>
#include <exceptions/exceptions.h>

// External symbols from linker script
extern char _kernel_end;

// External symbol from boot.S - contains detected physical base
extern uint64_t phys_base_storage;

// Global kernel physical base (used by VIRT_TO_PHYS/PHYS_TO_VIRT macros)
uint64_t kernel_phys_base;

// Forward declaration of common kernel_main
void kernel_main(void* dtb);

// ARM64-specific early initialization
void init_arm64(void* dtb) {
    /* 
     * At this point:
     * - MMU is enabled with identity and higher-half mappings
     * - We're running at virtual addresses
     * - DTB pointer is preserved from boot.S
     * - Stack is set up
     */
    
    // Initialize kernel physical base from boot.S detected value
    // phys_base_storage was set in physical memory, but we can read it
    // because we have identity mapping for the kernel region
    kernel_phys_base = phys_base_storage;
    
    // Install exception vectors early (before any interrupts can occur)
    // Uses the architecture-agnostic function that handles UART safely
    exception_init();
    
    // Jump to common kernel initialization
    kernel_main(dtb);
}

// Get current exception level (ARM64-specific)
int arch_get_exception_level(void) {
    uint64_t current_el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r" (current_el));
    return (current_el >> 2) & 0x3;
}
