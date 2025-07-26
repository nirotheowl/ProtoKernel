#include <uart.h>
#include <stddef.h>

#define UART0_BASE 0xFFFF000009000000
#define UART0_DR   *((volatile uint32_t*)(UART0_BASE + 0x00))
#define UART0_FR   *((volatile uint32_t*)(UART0_BASE + 0x18))

void uart_init(void) {
    // PL011 UART is already initialized by QEMU
    // In a real implementation, we'd configure baud rate, data bits, etc.
}

void uart_putc(char c) {
    // Wait for transmit FIFO to not be full
    while (UART0_FR & (1 << 5)) {
        __asm__ volatile("nop");
    }
    
    UART0_DR = c;
    
    // Ensure the write completes to device memory
    __asm__ volatile("dsb sy");
    
    // Wait for transmit to complete
    while (UART0_FR & (1 << 3)) {
        __asm__ volatile("nop");
    }
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
