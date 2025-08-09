/*
 * kernel/drivers/uart/uart.c
 *
 * UART driver implementation for serial console output
 */

#include <uart.h>
#include <stddef.h>
#include <memory/devmap.h>
#include <device/device.h>
#include <device/resource.h>
#include <arch_interface.h>

static volatile void *uart_base = NULL;
static const char *uart_compatible = NULL;

// PL011 UART registers (ARM)
#define PL011_DR    0x00
#define PL011_FR    0x18
#define PL011_FR_TXFF (1 << 5)  // Transmit FIFO Full
#define PL011_FR_BUSY (1 << 3)  // UART Busy

// NS16550 UART registers (common 16550-compatible)
#define NS16550_THR  0x00  // Transmit Holding Register
#define NS16550_LSR  0x05  // Line Status Register
#define NS16550_LSR_THRE (1 << 5)  // Transmit Holding Register Empty
#define NS16550_LSR_TEMT (1 << 6)  // Transmitter Empty

// Helper function to check if a compatible string contains a substring
static int uart_compat_contains(const char *compat, const char *substr) {
    if (!compat || !substr) return 0;
    
    const char *p = compat;
    const char *q;
    
    while (*p) {
        q = substr;
        const char *start = p;
        
        while (*p && *q && *p == *q) {
            p++;
            q++;
        }
        
        if (!*q) return 1;  // Found match
        
        p = start + 1;
    }
    
    return 0;
}

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
                uart_base = res->mapped_addr;
                uart_compatible = platform->console_uart_compatible;
                // Debug output removed - uart_base not yet set
                return;
            }
        }
    }
    
    // Try direct physical address lookup
    void *va = devmap_device_va(platform->console_uart_phys);
    if (va) {
        uart_base = va;
        uart_compatible = platform->console_uart_compatible;  // May be NULL
        // Debug output removed - uart_base not yet set
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
    
    // Determine UART type and use appropriate registers
    if (uart_compatible && 
        (uart_compat_contains(uart_compatible, "ns16550") ||
         uart_compat_contains(uart_compatible, "16550") ||
         uart_compat_contains(uart_compatible, "8250"))) {
        // NS16550-compatible UART (common for x86, RISC-V, some ARM)
        volatile uint8_t *uart = (volatile uint8_t *)uart_base;
        
        // Wait for transmit holding register to be empty
        while (!(uart[NS16550_LSR] & NS16550_LSR_THRE)) {
            arch_io_nop();
        }
        
        // Write the character
        uart[NS16550_THR] = c;
        
        // Ensure the write completes
        arch_io_barrier();
        
        // Wait for transmitter to be empty
        while (!(uart[NS16550_LSR] & NS16550_LSR_TEMT)) {
            arch_io_nop();
        }
    } else {
        // Default to PL011 for ARM platforms (QEMU virt, most ARM boards)
        /* This includes "arm,pl011", "arm,sbsa-uart", and Rockchip UARTs 
         * which are often PL011-compatible */
        volatile uint32_t *uart = (volatile uint32_t *)uart_base;
        
        // Wait for transmit FIFO to not be full
        while (uart[PL011_FR/4] & PL011_FR_TXFF) {
            arch_io_nop();
        }
        
        // Write the character
        uart[PL011_DR/4] = c;
        
        // Ensure the write completes to device memory
        arch_io_barrier();
        
        // Wait for transmit to complete
        while (uart[PL011_FR/4] & PL011_FR_BUSY) {
            arch_io_nop();
        }
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
