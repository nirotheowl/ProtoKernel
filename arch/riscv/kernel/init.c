/*
 * arch/riscv/kernel/init.c
 * 
 * RISC-V C entry and initialization
 */

#include <stdint.h>
#include <stddef.h>
#include <arch_cache.h>
#include <arch_exceptions.h>

// External symbols from linker script
extern char _kernel_end;
extern char _kernel_phys_base;

// Global kernel physical base (used by VIRT_TO_PHYS/PHYS_TO_VIRT macros)
uint64_t kernel_phys_base;

// Forward declaration for kernel_main
void kernel_main(void* dtb);

// Simple UART output for early debugging
static void early_putc(char c) {
    // QEMU RISC-V virt machine UART at 0x10000000 (NS16550 compatible)
    volatile uint8_t *uart = (volatile uint8_t *)0x10000000;
    
    // Wait for transmit ready (LSR bit 5)
    while (!(uart[5] & 0x20));
    
    // Write character
    uart[0] = c;
}

static void early_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            early_putc('\r');
        early_putc(*s++);
    }
}

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
    
    // Initialize kernel physical base from linker
    kernel_phys_base = (uint64_t)&_kernel_phys_base;
    
    // Initialize cache subsystem
    arch_cache_init();
    
    // Install trap/exception handlers
    arch_install_exception_handlers();
    
    // Print boot message for debugging
    early_puts("\n[RISC-V] Boot successful!\n");
    early_puts("[RISC-V] Hart ID: ");
    early_putc('0' + (hart_id & 0xF));  // Simple hex digit
    early_puts("\n[RISC-V] DTB at: 0x");
    
    // Print DTB address
    uintptr_t addr = (uintptr_t)dtb;
    for (int i = 60; i >= 0; i -= 4) {
        int digit = (addr >> i) & 0xF;
        early_putc(digit < 10 ? '0' + digit : 'a' + digit - 10);
    }
    early_puts("\n");
    
    early_puts("[RISC-V] Jumping to kernel_main...\n");
    
    // Jump to common kernel initialization
    // kernel_main(dtb);  // Currently commented out for testing
    
    // Test complete message (temporary - remove when kernel_main is enabled)
    early_puts("[RISC-V] RISC-V initialization test complete. Halting...\n");
    while (1) {
        __asm__ volatile("wfi");
    }
}
