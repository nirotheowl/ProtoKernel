/*
 * kernel/drivers/uart/uart_sifive.c
 * 
 * SiFive UART driver for RISC-V platforms
 * Supports SiFive FU540/FU740 and compatible UARTs
 */

#include <drivers/driver.h>
#include <drivers/driver_class.h>
#include <drivers/uart_drivers.h>
#include <drivers/driver_registry.h>
#include <device/device.h>
#include <device/resource.h>
#include <uart.h>
#include <string.h>
#include <memory/kmalloc.h>
#include <panic.h>

// SiFive UART registers (32-bit aligned)
#define SIFIVE_UART_TXDATA      0x00    // Transmit data register
#define SIFIVE_UART_RXDATA      0x04    // Receive data register
#define SIFIVE_UART_TXCTRL      0x08    // Transmit control register
#define SIFIVE_UART_RXCTRL      0x0C    // Receive control register
#define SIFIVE_UART_IE          0x10    // Interrupt enable register
#define SIFIVE_UART_IP          0x14    // Interrupt pending register
#define SIFIVE_UART_DIV         0x18    // Baud rate divisor register

// TXDATA register bits
#define SIFIVE_UART_TXDATA_DATA_MASK   0xFF        // Data bits [7:0]
#define SIFIVE_UART_TXDATA_FULL        (1 << 31)   // TX FIFO full

// RXDATA register bits
#define SIFIVE_UART_RXDATA_DATA_MASK   0xFF        // Data bits [7:0]
#define SIFIVE_UART_RXDATA_EMPTY       (1 << 31)   // RX FIFO empty

// TXCTRL register bits
#define SIFIVE_UART_TXCTRL_ENABLE      (1 << 0)    // Enable TX
#define SIFIVE_UART_TXCTRL_NSTOP       (1 << 1)    // Number of stop bits (0=1, 1=2)
#define SIFIVE_UART_TXCTRL_TXCNT_MASK  (7 << 16)   // TX FIFO watermark

// RXCTRL register bits
#define SIFIVE_UART_RXCTRL_ENABLE      (1 << 0)    // Enable RX
#define SIFIVE_UART_RXCTRL_RXCNT_MASK  (7 << 16)   // RX FIFO watermark

// IE register bits
#define SIFIVE_UART_IE_TXWM            (1 << 0)    // TX watermark interrupt enable
#define SIFIVE_UART_IE_RXWM            (1 << 1)    // RX watermark interrupt enable

// IP register bits
#define SIFIVE_UART_IP_TXWM            (1 << 0)    // TX watermark interrupt pending
#define SIFIVE_UART_IP_RXWM            (1 << 1)    // RX watermark interrupt pending

// SiFive UART driver private data
struct sifive_priv {
    bool initialized;       // Driver initialized flag
};

// Forward declaration
static int sifive_set_baudrate(struct uart_softc *sc, uint32_t baud);

// Device match table
static struct device_match sifive_matches[] = {
    { MATCH_COMPATIBLE, "sifive,uart0", NULL },
    { MATCH_COMPATIBLE, "sifive,fu540-c000-uart", NULL },
    { MATCH_COMPATIBLE, "sifive,fu740-c000-uart", NULL },
};

// UART operations implementation
static int sifive_init(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Disable interrupts
    regs[SIFIVE_UART_IE/4] = 0;
    
    // Set default baud rate (115200)
    sifive_set_baudrate(sc, 115200);
    
    // Enable TX and RX with 1 stop bit
    regs[SIFIVE_UART_TXCTRL/4] = SIFIVE_UART_TXCTRL_ENABLE;
    regs[SIFIVE_UART_RXCTRL/4] = SIFIVE_UART_RXCTRL_ENABLE;
    
    return 0;
}

static void sifive_putc(struct uart_softc *sc, char c) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Wait for TX FIFO to have space
    while (regs[SIFIVE_UART_TXDATA/4] & SIFIVE_UART_TXDATA_FULL) {
        // Spin
    }
    
    // Write character
    regs[SIFIVE_UART_TXDATA/4] = c;
}

static int sifive_getc(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    uint32_t rxdata;
    
    // Read RX data register
    rxdata = regs[SIFIVE_UART_RXDATA/4];
    
    // Check if RX FIFO is empty
    if (rxdata & SIFIVE_UART_RXDATA_EMPTY) {
        return -1;
    }
    
    // Return character
    return rxdata & SIFIVE_UART_RXDATA_DATA_MASK;
}

static bool sifive_readable(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    uint32_t rxdata = regs[SIFIVE_UART_RXDATA/4];
    return !(rxdata & SIFIVE_UART_RXDATA_EMPTY);
}

static int sifive_set_baudrate(struct uart_softc *sc, uint32_t baud) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    uint32_t divisor;
    
    if (baud == 0) {
        return -1;
    }
    
    // Calculate divisor
    // divisor = (clock_freq / baud) - 1
    divisor = (sc->clock_freq / baud) - 1;
    
    // Check for valid divisor range
    if (divisor > 0xFFFF) {
        return -1;
    }
    
    // Set divisor
    regs[SIFIVE_UART_DIV/4] = divisor;
    
    return 0;
}

static int sifive_set_format(struct uart_softc *sc, int databits, int stopbits, 
                             uart_parity_t parity)
{
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    uint32_t txctrl;
    
    // SiFive UART only supports 8N1 or 8N2
    if (databits != 8) {
        return -1;  // Only 8-bit data supported
    }
    
    if (parity != UART_PARITY_NONE) {
        return -1;  // No parity support
    }
    
    // Read current TX control
    txctrl = regs[SIFIVE_UART_TXCTRL/4];
    
    // Set stop bits
    if (stopbits == 2) {
        txctrl |= SIFIVE_UART_TXCTRL_NSTOP;
    } else if (stopbits == 1) {
        txctrl &= ~SIFIVE_UART_TXCTRL_NSTOP;
    } else {
        return -1;
    }
    
    // Write back TX control
    regs[SIFIVE_UART_TXCTRL/4] = txctrl;
    
    return 0;
}

static void sifive_enable_irq(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Set RX watermark to 0 (interrupt when any data available)
    uint32_t rxctrl = regs[SIFIVE_UART_RXCTRL/4];
    rxctrl &= ~SIFIVE_UART_RXCTRL_RXCNT_MASK;
    regs[SIFIVE_UART_RXCTRL/4] = rxctrl;
    
    // Enable RX watermark interrupt
    regs[SIFIVE_UART_IE/4] = SIFIVE_UART_IE_RXWM;
}

static void sifive_disable_irq(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Disable all interrupts
    regs[SIFIVE_UART_IE/4] = 0;
}

static void sifive_flush(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Wait for TX FIFO to be empty
    // SiFive UART doesn't have a direct TX empty flag,
    // so we check if we can write the full FIFO depth
    // This is a simplified approach
    while (regs[SIFIVE_UART_TXDATA/4] & SIFIVE_UART_TXDATA_FULL) {
        // Spin
    }
}

// UART class operations
static struct uart_ops sifive_uart_ops = {
    .init = sifive_init,
    .putc = sifive_putc,
    .getc = sifive_getc,
    .readable = sifive_readable,
    .set_baudrate = sifive_set_baudrate,
    .set_format = sifive_set_format,
    .enable_irq = sifive_enable_irq,
    .disable_irq = sifive_disable_irq,
    .flush = sifive_flush,
};

// UART class descriptor
static struct uart_class sifive_uart_class = {
    .name = "sifive",
    .ops = &sifive_uart_ops,
    .softc_size = sizeof(struct uart_softc),
    .capabilities = UART_CAP_FIFO,  // Basic FIFO support, no modem control
};

// Driver probe function
static int sifive_probe(struct device *dev) {
    const char *compat;
    
    // Get device compatible string
    compat = device_get_compatible(dev);
    if (!compat) {
        return PROBE_SCORE_NONE;
    }
    
    // Check for SiFive UART matches
    if (strstr(compat, "sifive,uart") || 
        strstr(compat, "sifive,fu540") ||
        strstr(compat, "sifive,fu740")) {
        return PROBE_SCORE_EXACT;
    }
    
    return PROBE_SCORE_NONE;
}

// Driver attach function
static int sifive_attach(struct device *dev) {
    struct uart_softc *sc;
    struct sifive_priv *priv;
    struct resource *res;
    volatile uint32_t *regs;
    
    uart_puts("SiFive: Attaching to device ");
    uart_puts(device_get_name(dev));
    uart_puts("\n");
    
    // Allocate software context
    sc = kmalloc(sizeof(*sc) + sizeof(*priv), KM_ZERO);
    if (!sc) {
        return -1;
    }
    priv = (struct sifive_priv *)(sc + 1);
    
    // Initialize UART software context
    uart_softc_init(sc, dev, &sifive_uart_class);
    
    // Set priv pointer AFTER uart_softc_init
    sc->priv = priv;
    
    // Check if device is mapped
    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res || !res->mapped_addr) {
        uart_puts("SiFive: Device not mapped\n");
        kfree(sc);
        return -1;
    }
    
    regs = (volatile uint32_t *)sc->regs;
    
    // Check if UART is already enabled
    if ((regs[SIFIVE_UART_TXCTRL/4] & SIFIVE_UART_TXCTRL_ENABLE) &&
        (regs[SIFIVE_UART_RXCTRL/4] & SIFIVE_UART_RXCTRL_ENABLE)) {
        uart_puts("SiFive: UART already enabled by bootloader\n");
        priv->initialized = true;
        
        // Just disable interrupts for now
        regs[SIFIVE_UART_IE/4] = 0;
    } else {
        // Initialize hardware
        if (sifive_init(sc) != 0) {
            uart_puts("SiFive: Failed to initialize hardware\n");
            kfree(sc);
            return -1;
        }
        priv->initialized = true;
    }
    
    // Store context in device
    device_set_driver_data(dev, sc);
    
    // Mark device as active
    device_activate(dev);
    
    uart_puts("SiFive: Device attached successfully at ");
    uart_puthex((uint64_t)sc->regs);
    uart_puts("\n");
    
    // Check if this should be the console
    // TODO: Check FDT for stdout-path or console property
    
    return 0;
}

// Driver detach function
static int sifive_detach(struct device *dev) {
    struct uart_softc *sc;
    struct sifive_priv *priv;
    
    uart_puts("SiFive: Detaching from device ");
    uart_puts(device_get_name(dev));
    uart_puts("\n");
    
    // Get software context
    sc = device_get_driver_data(dev);
    if (sc) {
        priv = (struct sifive_priv *)sc->priv;
        
        if (priv && priv->initialized) {
            // Disable interrupts
            sifive_disable_irq(sc);
            priv->initialized = false;
        }
        
        // Free context
        kfree(sc);
    }
    
    // Clear driver data
    device_set_driver_data(dev, NULL);
    
    // Mark device as inactive
    device_deactivate(dev);
    
    return 0;
}

// Driver operations structure
static struct driver_ops sifive_driver_ops = {
    .probe = sifive_probe,
    .attach = sifive_attach,
    .detach = sifive_detach,
    .suspend = NULL,
    .resume = NULL,
    .ioctl = NULL,
};

// Driver structure
static struct driver sifive_driver = {
    .name = "sifive_uart",
    .class = DRIVER_CLASS_UART,
    .ops = &sifive_driver_ops,
    .matches = sifive_matches,
    .num_matches = sizeof(sifive_matches) / sizeof(sifive_matches[0]),
    .priority = 10,
    .priv_size = sizeof(struct uart_softc) + sizeof(struct sifive_priv),
    .flags = DRIVER_FLAG_BUILTIN,
};

// Driver initialization function
static void sifive_driver_init(void) {
    int ret;
    
    uart_puts("SiFive: Registering driver\n");
    
    ret = driver_register(&sifive_driver);
    if (ret == 0) {
        uart_puts("SiFive: Driver registered successfully\n");
    } else {
        uart_puts("SiFive: Failed to register driver\n");
    }
}

UART_DRIVER_REGISTER(sifive_driver_init, DRIVER_PRIO_NORMAL);
