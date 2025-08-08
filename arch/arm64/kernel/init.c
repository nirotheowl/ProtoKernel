/*
 * arch/arm64/kernel/init.c
 *
 * ARM64-specific initialization
 */

#include <stdint.h>
#include <stddef.h>

/* External symbols from linker script */
extern char _kernel_end;

/* Forward declaration of common kernel_main */
void kernel_main(void* dtb);

/* ARM64-specific early initialization */
void init_arm64(void* dtb) {
    /* 
     * At this point:
     * - MMU is enabled with identity and higher-half mappings
     * - We're running at virtual addresses
     * - DTB pointer is preserved from boot.S
     * - Stack is set up
     */
    
    /* TODO: ARM64-specific initialization if needed */
    /* For now, just call the common kernel_main */
    
    /* Jump to common kernel initialization */
    kernel_main(dtb);
}

/* Get current exception level (ARM64-specific) */
int arch_get_exception_level(void) {
    uint64_t current_el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r" (current_el));
    return (current_el >> 2) & 0x3;
}