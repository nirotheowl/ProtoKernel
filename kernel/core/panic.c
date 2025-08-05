/*
 * kernel/core/panic.c
 *
 * Kernel panic handling functions
 */

#include <panic.h>
#include <uart.h>
#include <stdarg.h>
#include <stddef.h>

// Architecture-specific halt implementation
static void arch_halt(void) __attribute__((noreturn));

static void arch_halt(void) {
    // Disable all interrupts
    __asm__ volatile("msr daifset, #0xf");
    
    // Infinite loop with wait-for-event to save power
    while (1) {
        __asm__ volatile("wfe");
    }
}

// Common panic finish routine
static void panic_finish(void) __attribute__((noreturn));

static void panic_finish(void) {
    // Try to output final message if UART is available
    uart_puts("System halted.\n");
    
    // TODO: Future enhancements:
    // - Dump registers
    // - Stack trace
    // - Memory dump
    // - Reboot after timeout (if configured)
    // - Early panic handler that doesn't rely on UART
    
    arch_halt();
}

// Panic with formatted message
void panic(const char *fmt, ...) {
    va_list args;
    
    // Output panic header - will be silent if UART not initialized
    uart_puts("\n*** KERNEL PANIC ***\n");
    
    // Output formatted message if UART is available
    if (fmt) {
        va_start(args, fmt);
        // TODO: Implement vprintf or use simpler formatting
        uart_puts(fmt);  // For now, just print the format string
        va_end(args);
        uart_puts("\n");
    }
    
    panic_finish();
}

// Simple panic with string message
void panic_str(const char *str) {
    // Output will be silent if UART not initialized
    uart_puts("\n*** KERNEL PANIC ***\n");
    
    if (str) {
        uart_puts(str);
        uart_puts("\n");
    }
    
    panic_finish();
}