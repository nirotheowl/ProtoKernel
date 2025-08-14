/*
 * kernel/drivers/uart/uart_core.c
 * 
 * UART framework core implementation
 * Provides common UART functionality and manages UART devices
 */

#include <drivers/driver.h>
#include <drivers/driver_class.h>
#include <device/device.h>
#include <device/resource.h>
#include <uart.h>
#include <string.h>
#include <memory/kmalloc.h>
#include <panic.h>

// Console UART tracking
static struct uart_softc *console_uart = NULL;
static struct uart_softc *boot_uart = NULL;

// UART device list
static struct uart_softc *uart_devices = NULL;
static uint32_t uart_count = 0;

// Get UART software context from device
struct uart_softc *uart_device_get_softc(struct device *dev) {
    if (!dev || !dev->driver) {
        return NULL;
    }
    
    // Check if this is a UART device
    if (dev->driver->class != DRIVER_CLASS_UART) {
        return NULL;
    }
    
    return (struct uart_softc *)device_get_driver_data(dev);
}

// Register a UART device
static int uart_register_device(struct uart_softc *sc) {
    if (!sc) {
        return -1;
    }
    
    // Add to device list (simple linked list for now)
    sc->priv = uart_devices;
    uart_devices = sc;
    uart_count++;
    
    return 0;
}

// Initialize UART software context
int uart_softc_init(struct uart_softc *sc, struct device *dev, const struct uart_class *class) {
    if (!sc || !dev || !class) {
        return -1;
    }
    
    // Initialize base fields
    sc->dev = dev;
    sc->class = class;
    sc->regs = NULL;
    sc->clock_freq = 0;
    sc->baudrate = 115200;  // Default baud rate
    sc->databits = 8;
    sc->stopbits = 1;
    sc->parity = UART_PARITY_NONE;
    sc->flags = 0;
    
    // Get memory resource
    struct resource *res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (res && res->mapped_addr) {
        sc->regs = res->mapped_addr;
    }
    
    // Get clock frequency
    sc->clock_freq = device_get_clock_freq(dev, 0);
    if (sc->clock_freq == 0) {
        // Try to get from device property
        // Default to 24MHz if not specified
        sc->clock_freq = 24000000;
    }
    
    // Register this UART device
    uart_register_device(sc);
    
    return 0;
}

// UART putc wrapper
void uart_softc_putc(struct uart_softc *sc, char c) {
    if (!sc || !sc->class || !sc->class->ops || !sc->class->ops->putc) {
        return;
    }
    
    sc->class->ops->putc(sc, c);
}

// UART getc wrapper
int uart_softc_getc(struct uart_softc *sc) {
    if (!sc || !sc->class || !sc->class->ops || !sc->class->ops->getc) {
        return -1;
    }
    
    return sc->class->ops->getc(sc);
}

// UART readable check wrapper
bool uart_softc_readable(struct uart_softc *sc) {
    if (!sc || !sc->class || !sc->class->ops || !sc->class->ops->readable) {
        return false;
    }
    
    return sc->class->ops->readable(sc);
}

// Set UART baud rate
int uart_softc_set_baudrate(struct uart_softc *sc, uint32_t baud) {
    if (!sc || !sc->class || !sc->class->ops || !sc->class->ops->set_baudrate) {
        return -1;
    }
    
    int ret = sc->class->ops->set_baudrate(sc, baud);
    if (ret == 0) {
        sc->baudrate = baud;
    }
    
    return ret;
}

// Set UART format
int uart_softc_set_format(struct uart_softc *sc, int databits, int stopbits, 
                         uart_parity_t parity)
{
    if (!sc || !sc->class || !sc->class->ops || !sc->class->ops->set_format) {
        return -1;
    }
    
    int ret = sc->class->ops->set_format(sc, databits, stopbits, parity);
    if (ret == 0) {
        sc->databits = databits;
        sc->stopbits = stopbits;
        sc->parity = parity;
    }
    
    return ret;
}

// Attach UART as console
int uart_console_attach(struct uart_softc *sc) {
    if (!sc) {
        return -1;
    }
    
    // If no console yet, use this one
    if (!console_uart) {
        console_uart = sc;
        uart_puts("UART: Console attached to ");
        uart_puts(sc->dev->name);
        uart_puts("\n");
        return 0;
    }
    
    return -1;
}

// Get console UART
struct uart_softc *uart_console_get(void) {
    return console_uart;
}

// Set boot UART (early console)
void uart_boot_set(struct uart_softc *sc) {
    boot_uart = sc;
}

// Get boot UART
struct uart_softc *uart_boot_get(void) {
    return boot_uart;
}

// Console output functions using UART framework
void uart_console_putc(char c) {
    if (console_uart) {
        uart_softc_putc(console_uart, c);
    } else if (boot_uart) {
        uart_softc_putc(boot_uart, c);
    }
    // Fallback to early console handled elsewhere
}

int uart_console_getc(void) {
    if (console_uart) {
        return uart_softc_getc(console_uart);
    } else if (boot_uart) {
        return uart_softc_getc(boot_uart);
    }
    return -1;
}

bool uart_console_readable(void) {
    if (console_uart) {
        return uart_softc_readable(console_uart);
    } else if (boot_uart) {
        return uart_softc_readable(boot_uart);
    }
    return false;
}

// Print UART device information
void uart_print_info(struct uart_softc *sc) {
    if (!sc) {
        return;
    }
    
    uart_puts("UART Device: ");
    if (sc->dev) {
        uart_puts(sc->dev->name);
    } else {
        uart_puts("(unnamed)");
    }
    uart_puts("\n");
    
    uart_puts("  Class: ");
    if (sc->class) {
        uart_puts(sc->class->name);
    } else {
        uart_puts("(none)");
    }
    uart_puts("\n");
    
    uart_puts("  Base: ");
    uart_puthex((uint64_t)sc->regs);
    uart_puts("\n");
    
    uart_puts("  Clock: ");
    uart_putdec(sc->clock_freq);
    uart_puts(" Hz\n");
    
    uart_puts("  Format: ");
    uart_putdec(sc->baudrate);
    uart_puts(" ");
    uart_putdec(sc->databits);
    switch (sc->parity) {
        case UART_PARITY_NONE: uart_puts("N"); break;
        case UART_PARITY_ODD: uart_puts("O"); break;
        case UART_PARITY_EVEN: uart_puts("E"); break;
        default: uart_puts("?"); break;
    }
    uart_putdec(sc->stopbits);
    uart_puts("\n");
    
    if (sc == console_uart) {
        uart_puts("  [CONSOLE]\n");
    }
    if (sc == boot_uart) {
        uart_puts("  [BOOT]\n");
    }
}

// List all UART devices
void uart_list_devices(void) {
    struct uart_softc *sc;
    
    uart_puts("UART Devices:\n");
    uart_puts("-------------\n");
    
    for (sc = uart_devices; sc; sc = (struct uart_softc *)sc->priv) {
        uart_print_info(sc);
    }
    
    uart_puts("Total UARTs: ");
    uart_putdec(uart_count);
    uart_puts("\n");
}

// Initialize UART framework
int uart_framework_init(void) {
    console_uart = NULL;
    boot_uart = NULL;
    uart_devices = NULL;
    uart_count = 0;
    
    uart_puts("UART framework initialized\n");
    
    return 0;
}

// Auto-detect and select console UART
// This should be called after FDT parsing
int uart_console_autodetect(void) {
    struct device *dev;
    struct uart_softc *sc;
    
    // First, check if we already have a console
    if (console_uart) {
        return 0;
    }
    
    // Look for a UART device marked as console
    dev = device_get_root();
    while (dev) {
        if (dev->type == DEV_TYPE_UART && dev->active) {
            sc = uart_device_get_softc(dev);
            if (sc) {
                // Check if this device has a "console" property
                // For now, just use the first active UART
                uart_console_attach(sc);
                return 0;
            }
        }
        dev = dev->next;
    }
    
    // No suitable UART found
    uart_puts("UART: No console device found\n");
    return -1;
}
