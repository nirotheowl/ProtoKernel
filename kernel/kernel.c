#include <stdint.h>
#include <stddef.h>
#include "display.h"

#define UART0_BASE 0x09000000
#define UART0_DR   *((volatile uint32_t*)(UART0_BASE + 0x00))
#define UART0_FR   *((volatile uint32_t*)(UART0_BASE + 0x18))

void uart_putc(char c) {
    while (UART0_FR & (1 << 5));
    UART0_DR = c;
}

void uart_puts(const char* str) {
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str++);
    }
}

void uart_puthex(uint64_t value) {
    int i;
    
    uart_puts("0x");
    
    for (i = 15; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        if (nibble < 10) {
            uart_putc('0' + nibble);
        } else {
            uart_putc('A' + nibble - 10);
        }
    }
}

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
    
    uart_puts("\nInitializing display...\n");
    display_init();
    
    // Draw a welcome screen
    // Background color
    display_clear(0xFF2C3E50);  // Dark blue-gray background
    
    // Draw title
    display_draw_string(250, 100, "MICL ARM64 OS", 0xFFFFFFFF, 0xFF2C3E50);
    display_draw_string(200, 130, "Running on QEMU virt machine", 0xFFECF0F1, 0xFF2C3E50);
    
    // Draw some colored rectangles
    display_draw_rect(100, 200, 100, 50, 0xFFE74C3C);  // Red
    display_draw_rect(220, 200, 100, 50, 0xFF2ECC71);  // Green
    display_draw_rect(340, 200, 100, 50, 0xFF3498DB);  // Blue
    display_draw_rect(460, 200, 100, 50, 0xFFF39C12);  // Yellow
    
    // System info on screen
    display_draw_string(100, 300, "System Information:", 0xFFFFFFFF, 0xFF2C3E50);
    display_draw_string(100, 320, "- Exception Level: EL1", 0xFFBDC3C7, 0xFF2C3E50);
    display_draw_string(100, 340, "- Display: 800x600 32bpp", 0xFFBDC3C7, 0xFF2C3E50);
    display_draw_string(100, 360, "- CPU: Cortex-A53", 0xFFBDC3C7, 0xFF2C3E50);
    
    uart_puts("\nSystem Information:\n");
    uart_puts("- UART Base: ");
    uart_puthex(UART0_BASE);
    uart_puts("\n");
    
    uart_puts("- Kernel loaded at: ");
    extern char __kernel_start;
    uart_puthex((uint64_t)&__kernel_start);
    uart_puts("\n");
    
    uart_puts("- Display initialized: 800x600 @ 32bpp\n");
    
    uart_puts("\nKernel initialization complete!\n");
    uart_puts("System halted.\n");
    
    while (1) {
        __asm__ volatile("wfe");
    }
}