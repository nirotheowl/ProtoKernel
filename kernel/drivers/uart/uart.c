#include <uart.h>
#include <stddef.h>
#include <platform/devmap.h>

static volatile uint32_t *uart_base = NULL;

#define UART0_DR   (uart_base[0x00/4])
#define UART0_FR   (uart_base[0x18/4])

void uart_init(void) {
    // Use temporary boot.S mapping initially
    uart_base = (volatile uint32_t *)0xFFFF000009000000UL;
    
    // PL011 UART is already initialized by QEMU
    // In a real implementation, we'd configure baud rate, data bits, etc.
}

void uart_update_base(void) {
    // Called after devmap is initialized to set UART base
    uart_base = (volatile uint32_t *)devmap_device_va(0x09000000);
}

void uart_putc(char c) {
    // Early return if UART not yet initialized
    if (!uart_base) {
        return;
    }
    
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
