#include <uart.h>
#include <stddef.h>
#include <memory/devmap.h>
#include <device/device.h>
#include <device/resource.h>

static volatile uint32_t *uart_base = NULL;

#define UART0_DR   (uart_base[0x00/4])
#define UART0_FR   (uart_base[0x18/4])

void uart_init(void) {
    const platform_desc_t *platform = platform_get_current();
    
    // Platform must specify console UART
    if (!platform || !platform->console_uart_phys) {
        // No platform or no console UART specified - remain silent
        uart_base = NULL;
        return;
    }
    
    // Try to find by compatible string if provided
    if (platform->console_uart_compatible) {
        struct device *uart_dev = device_find_by_compatible(platform->console_uart_compatible);
        if (uart_dev) {
            struct resource *res = device_get_resource(uart_dev, RES_TYPE_MEM, 0);
            if (res && res->mapped_addr) {
                uart_base = (volatile uint32_t *)res->mapped_addr;
                uart_puts("UART: Using ");
                uart_puts(platform->console_uart_compatible);
                uart_puts(" at VA ");
                uart_puthex((uint64_t)res->mapped_addr);
                uart_puts(" (PA ");
                uart_puthex(res->start);
                uart_puts(")\n");
                return;
            }
        }
    }
    
    // Try direct physical address lookup
    void *va = devmap_device_va(platform->console_uart_phys);
    if (va) {
        uart_base = (volatile uint32_t *)va;
        uart_puts("UART: Using device at PA ");
        uart_puthex(platform->console_uart_phys);
        uart_puts(" (no compatible string match)\n");
        return;
    }
    
    // No valid mapping found - remain silent
    uart_base = NULL;
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

void uart_putdec(uint64_t value) {
    char buffer[21];  // Max 20 digits for 64-bit number + null terminator
    int i = 20;
    
    // Handle zero case
    if (value == 0) {
        uart_putc('0');
        return;
    }
    
    // Null terminate the buffer
    buffer[i] = '\0';
    
    // Build the string from right to left
    while (value > 0 && i > 0) {
        i--;
        buffer[i] = '0' + (value % 10);
        value /= 10;
    }
    
    // Output the string
    uart_puts(&buffer[i]);
}
