/*
 * kernel/drivers/uart/uart.c
 * 
 * Temporary UART functions used for kernel development 
 */

#include <uart.h>
#include <drivers/driver.h>
#include <drivers/uart_drivers.h>
#include <drivers/driver_registry.h>
#include <device/device.h>
#include <string.h>

// Current console UART
static struct uart_softc *console_uart = NULL;

void uart_init(void) {
    // Initialize UART framework
    uart_framework_init();
    
    // Automatically register all UART drivers from linker section
    driver_registry_init_uart();
}

void uart_putc(char c) {
    // Get console from framework
    if (!console_uart) {
        console_uart = uart_console_get();
    }
    
    if (!console_uart || !console_uart->class || !console_uart->class->ops) {
        return;
    }
    
    if (console_uart->class->ops->putc) {
        console_uart->class->ops->putc(console_uart, c);
    }
}

void uart_puts(const char *str) {
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

void uart_putdec(uint64_t value) {
    char buffer[21];
    int i = 20;
    
    if (value == 0) {
        uart_putc('0');
        return;
    }
    
    buffer[i] = '\0';
    
    while (value > 0 && i > 0) {
        i--;
        buffer[i] = '0' + (value % 10);
        value /= 10;
    }
    
    uart_puts(&buffer[i]);
}
