/*
 * kernel/drivers/uart/uart_ns16550.c
 * 
 * NS16550/8250 UART driver using the new driver framework
 * Supports standard 16550-compatible UARTs found on many platforms
 */

#include <drivers/driver.h>
#include <drivers/driver_class.h>
#include <drivers/uart_drivers.h>
#include <device/device.h>
#include <device/resource.h>
#include <uart.h>
#include <string.h>
#include <memory/kmalloc.h>
#include <panic.h>

// NS16550 UART registers (byte offsets)
#define NS16550_RBR     0x00    // Receive Buffer Register (RO)
#define NS16550_THR     0x00    // Transmit Holding Register (WO)
#define NS16550_DLL     0x00    // Divisor Latch Low (DLAB=1)
#define NS16550_IER     0x01    // Interrupt Enable Register
#define NS16550_DLH     0x01    // Divisor Latch High (DLAB=1)
#define NS16550_IIR     0x02    // Interrupt Identification Register (RO)
#define NS16550_FCR     0x02    // FIFO Control Register (WO)
#define NS16550_LCR     0x03    // Line Control Register
#define NS16550_MCR     0x04    // Modem Control Register
#define NS16550_LSR     0x05    // Line Status Register
#define NS16550_MSR     0x06    // Modem Status Register
#define NS16550_SCR     0x07    // Scratch Register

// Line Status Register bits
#define NS16550_LSR_DR      0x01    // Data Ready
#define NS16550_LSR_OE      0x02    // Overrun Error
#define NS16550_LSR_PE      0x04    // Parity Error
#define NS16550_LSR_FE      0x08    // Framing Error
#define NS16550_LSR_BI      0x10    // Break Interrupt
#define NS16550_LSR_THRE    0x20    // THR Empty
#define NS16550_LSR_TEMT    0x40    // Transmitter Empty
#define NS16550_LSR_FIFOE   0x80    // FIFO Error

// Line Control Register bits
#define NS16550_LCR_DLAB    0x80    // Divisor Latch Access Bit
#define NS16550_LCR_SBC     0x40    // Set Break Control
#define NS16550_LCR_SPAR    0x20    // Stick Parity
#define NS16550_LCR_EPS     0x10    // Even Parity Select
#define NS16550_LCR_PEN     0x08    // Parity Enable
#define NS16550_LCR_STOP    0x04    // Stop bits (0=1, 1=2)
#define NS16550_LCR_WLS_MASK 0x03   // Word Length Select mask

// FIFO Control Register bits
#define NS16550_FCR_ENABLE  0x01    // Enable FIFOs
#define NS16550_FCR_CLRRECV 0x02    // Clear Receive FIFO
#define NS16550_FCR_CLRXMIT 0x04    // Clear Transmit FIFO
#define NS16550_FCR_DMA     0x08    // DMA Mode Select
#define NS16550_FCR_TRIGGER_1  0x00 // Trigger at 1 byte
#define NS16550_FCR_TRIGGER_4  0x40 // Trigger at 4 bytes
#define NS16550_FCR_TRIGGER_8  0x80 // Trigger at 8 bytes
#define NS16550_FCR_TRIGGER_14 0xC0 // Trigger at 14 bytes

// Modem Control Register bits
#define NS16550_MCR_DTR     0x01    // Data Terminal Ready
#define NS16550_MCR_RTS     0x02    // Request To Send
#define NS16550_MCR_OUT1    0x04    // Output 1
#define NS16550_MCR_OUT2    0x08    // Output 2 (enables interrupts)
#define NS16550_MCR_LOOP    0x10    // Loopback mode

// NS16550 driver private data
struct ns16550_priv {
    uint32_t reg_shift;     // Register shift (0, 1, or 2)
    uint32_t reg_width;     // Register width (1, 2, or 4 bytes)
    bool has_fifo;          // Has FIFOs
    uint32_t fifo_size;     // FIFO size (16, 64, etc.)
};

// Device-specific configurations
static struct ns16550_priv dw_apb_config = {
    .reg_shift = 2,         // 32-bit aligned registers
    .reg_width = 4,
    .has_fifo = true,
    .fifo_size = 16,
};

// Forward declaration
static int ns16550_set_baudrate(struct uart_softc *sc, uint32_t baud);

// Device match table
static struct device_match ns16550_matches[] = {
    { MATCH_COMPATIBLE, "ns16550", NULL },
    { MATCH_COMPATIBLE, "ns16550a", NULL },
    { MATCH_COMPATIBLE, "ns8250", NULL },
    { MATCH_COMPATIBLE, "8250", NULL },
    { MATCH_COMPATIBLE, "16550", NULL },
    { MATCH_COMPATIBLE, "16750", NULL },
    { MATCH_COMPATIBLE, "snps,dw-apb-uart", &dw_apb_config },
};

// Helper function to read register
static uint32_t ns16550_read_reg(struct uart_softc *sc, uint32_t reg) {
    struct ns16550_priv *priv = (struct ns16550_priv *)sc->priv;
    volatile uint8_t *base8 = (volatile uint8_t *)sc->regs;
    volatile uint16_t *base16 = (volatile uint16_t *)sc->regs;
    volatile uint32_t *base32 = (volatile uint32_t *)sc->regs;
    uint32_t offset = reg << priv->reg_shift;
    
    switch (priv->reg_width) {
        case 1:
            return base8[offset];
        case 2:
            return base16[offset / 2];
        case 4:
            return base32[offset / 4];
        default:
            return base8[offset];
    }
}

// Helper function to write register
static void ns16550_write_reg(struct uart_softc *sc, uint32_t reg, uint32_t val) {
    struct ns16550_priv *priv = (struct ns16550_priv *)sc->priv;
    volatile uint8_t *base8 = (volatile uint8_t *)sc->regs;
    volatile uint16_t *base16 = (volatile uint16_t *)sc->regs;
    volatile uint32_t *base32 = (volatile uint32_t *)sc->regs;
    uint32_t offset = reg << priv->reg_shift;
    
    switch (priv->reg_width) {
        case 1:
            base8[offset] = val;
            break;
        case 2:
            base16[offset / 2] = val;
            break;
        case 4:
            base32[offset / 4] = val;
            break;
        default:
            base8[offset] = val;
            break;
    }
}

// UART operations implementation
static int ns16550_init(struct uart_softc *sc) {
    struct ns16550_priv *priv = (struct ns16550_priv *)sc->priv;
    
    // Disable interrupts
    ns16550_write_reg(sc, NS16550_IER, 0x00);
    
    // Enable FIFOs if available
    if (priv->has_fifo) {
        ns16550_write_reg(sc, NS16550_FCR, 
            NS16550_FCR_ENABLE | NS16550_FCR_CLRRECV | 
            NS16550_FCR_CLRXMIT | NS16550_FCR_TRIGGER_14);
    }
    
    // Set default line control: 8N1
    ns16550_write_reg(sc, NS16550_LCR, 0x03);  // 8 bits, no parity, 1 stop
    
    // Enable modem control
    ns16550_write_reg(sc, NS16550_MCR, NS16550_MCR_DTR | NS16550_MCR_RTS);
    
    // Set default baud rate (115200)
    return ns16550_set_baudrate(sc, 115200);
}

static void ns16550_putc(struct uart_softc *sc, char c) {
    // Wait for transmitter to be ready
    while (!(ns16550_read_reg(sc, NS16550_LSR) & NS16550_LSR_THRE)) {
        // Spin
    }
    
    // Write character
    ns16550_write_reg(sc, NS16550_THR, c);
}

static int ns16550_getc(struct uart_softc *sc) {
    // Check if data is available
    if (!(ns16550_read_reg(sc, NS16550_LSR) & NS16550_LSR_DR)) {
        return -1;
    }
    
    // Read character
    return ns16550_read_reg(sc, NS16550_RBR);
}

static bool ns16550_readable(struct uart_softc *sc) {
    return (ns16550_read_reg(sc, NS16550_LSR) & NS16550_LSR_DR) != 0;
}

static int ns16550_set_baudrate(struct uart_softc *sc, uint32_t baud) {
    uint32_t divisor;
    uint8_t lcr;
    
    if (baud == 0) {
        return -1;
    }
    
    // Calculate divisor
    divisor = sc->clock_freq / (16 * baud);
    if (divisor == 0 || divisor > 0xFFFF) {
        return -1;
    }
    
    // Save LCR
    lcr = ns16550_read_reg(sc, NS16550_LCR);
    
    // Enable DLAB to access divisor registers
    ns16550_write_reg(sc, NS16550_LCR, lcr | NS16550_LCR_DLAB);
    
    // Set divisor
    ns16550_write_reg(sc, NS16550_DLL, divisor & 0xFF);
    ns16550_write_reg(sc, NS16550_DLH, (divisor >> 8) & 0xFF);
    
    // Restore LCR (disables DLAB)
    ns16550_write_reg(sc, NS16550_LCR, lcr);
    
    return 0;
}

static int ns16550_set_format(struct uart_softc *sc, int databits, int stopbits, 
                              uart_parity_t parity)
{
    uint8_t lcr = 0;
    
    // Set data bits
    switch (databits) {
        case 5: lcr |= 0x00; break;
        case 6: lcr |= 0x01; break;
        case 7: lcr |= 0x02; break;
        case 8: lcr |= 0x03; break;
        default: return -1;
    }
    
    // Set stop bits
    if (stopbits == 2) {
        lcr |= NS16550_LCR_STOP;
    } else if (stopbits != 1) {
        return -1;
    }
    
    // Set parity
    switch (parity) {
        case UART_PARITY_NONE:
            break;
        case UART_PARITY_ODD:
            lcr |= NS16550_LCR_PEN;
            break;
        case UART_PARITY_EVEN:
            lcr |= NS16550_LCR_PEN | NS16550_LCR_EPS;
            break;
        case UART_PARITY_MARK:
            lcr |= NS16550_LCR_PEN | NS16550_LCR_SPAR;
            break;
        case UART_PARITY_SPACE:
            lcr |= NS16550_LCR_PEN | NS16550_LCR_EPS | NS16550_LCR_SPAR;
            break;
        default:
            return -1;
    }
    
    // Write LCR
    ns16550_write_reg(sc, NS16550_LCR, lcr);
    
    return 0;
}

static void ns16550_enable_irq(struct uart_softc *sc) {
    // Enable receive data available interrupt
    ns16550_write_reg(sc, NS16550_IER, 0x01);
    
    // Enable interrupts via MCR
    uint8_t mcr = ns16550_read_reg(sc, NS16550_MCR);
    ns16550_write_reg(sc, NS16550_MCR, mcr | NS16550_MCR_OUT2);
}

static void ns16550_disable_irq(struct uart_softc *sc) {
    // Disable all interrupts
    ns16550_write_reg(sc, NS16550_IER, 0x00);
}

static void ns16550_flush(struct uart_softc *sc) {
    struct ns16550_priv *priv = (struct ns16550_priv *)sc->priv;
    
    if (priv->has_fifo) {
        // Clear FIFOs
        ns16550_write_reg(sc, NS16550_FCR, 
            NS16550_FCR_ENABLE | NS16550_FCR_CLRRECV | NS16550_FCR_CLRXMIT);
    }
    
    // Wait for transmitter to be empty
    while (!(ns16550_read_reg(sc, NS16550_LSR) & NS16550_LSR_TEMT)) {
        // Spin
    }
}

// UART class operations
static struct uart_ops ns16550_uart_ops = {
    .init = ns16550_init,
    .putc = ns16550_putc,
    .getc = ns16550_getc,
    .readable = ns16550_readable,
    .set_baudrate = ns16550_set_baudrate,
    .set_format = ns16550_set_format,
    .enable_irq = ns16550_enable_irq,
    .disable_irq = ns16550_disable_irq,
    .flush = ns16550_flush,
};

// UART class descriptor
static struct uart_class ns16550_uart_class = {
    .name = "ns16550",
    .ops = &ns16550_uart_ops,
    .softc_size = sizeof(struct uart_softc),
    .capabilities = UART_CAP_FIFO | UART_CAP_MODEM,
};

// Driver probe function
static int ns16550_probe(struct device *dev) {
    const char *compat;
    
    // Get device compatible string
    compat = device_get_compatible(dev);
    if (!compat) {
        return PROBE_SCORE_NONE;
    }
    
    // Check for exact matches
    for (int i = 0; i < sizeof(ns16550_matches) / sizeof(ns16550_matches[0]); i++) {
        if (strcmp(compat, ns16550_matches[i].value) == 0) {
            return PROBE_SCORE_EXACT;
        }
    }
    
    // Check for partial matches
    if (strstr(compat, "16550") || strstr(compat, "8250")) {
        return PROBE_SCORE_GENERIC;
    }
    
    return PROBE_SCORE_NONE;
}

// Driver attach function
static int ns16550_attach(struct device *dev) {
    struct uart_softc *sc;
    struct ns16550_priv *priv;
    struct resource *res;
    const struct device_match *match = NULL;
    const char *compat;
    
    
    // Allocate software context
    sc = kmalloc(sizeof(*sc) + sizeof(*priv), KM_ZERO);
    if (!sc) {
        return -1;
    }
    priv = (struct ns16550_priv *)(sc + 1);
    
    // Find matching configuration
    compat = device_get_compatible(dev);
    if (compat) {
        for (int i = 0; i < sizeof(ns16550_matches) / sizeof(ns16550_matches[0]); i++) {
            if (strcmp(compat, ns16550_matches[i].value) == 0) {
                match = &ns16550_matches[i];
                break;
            }
        }
    }
    
    // Apply configuration
    if (match && match->driver_data) {
        // Use device-specific configuration
        *priv = *(struct ns16550_priv *)match->driver_data;
    } else {
        // Use default configuration
        priv->reg_shift = 0;
        priv->reg_width = 1;
        priv->has_fifo = true;
        priv->fifo_size = 16;
    }
    
    // Initialize UART software context
    uart_softc_init(sc, dev, &ns16550_uart_class);
    
    // Set priv pointer AFTER uart_softc_init
    sc->priv = priv;
    
    // Check if device is mapped
    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res || !res->mapped_addr) {
        kfree(sc);
        return -1;
    }
    
    // Initialize hardware
    if (ns16550_init(sc) != 0) {
        kfree(sc);
        return -1;
    }
    
    // Store context in device
    device_set_driver_data(dev, sc);
    
    // Mark device as active
    device_activate(dev);
    
    
    // Check if this should be the console
    // TODO: Check FDT for stdout-path or console property
    
    return 0;
}

// Driver detach function
static int ns16550_detach(struct device *dev) {
    struct uart_softc *sc;
    
    
    // Get software context
    sc = device_get_driver_data(dev);
    if (sc) {
        // Disable interrupts
        ns16550_disable_irq(sc);
        
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
static struct driver_ops ns16550_driver_ops = {
    .probe = ns16550_probe,
    .attach = ns16550_attach,
    .detach = ns16550_detach,
    .suspend = NULL,
    .resume = NULL,
    .ioctl = NULL,
};

// Driver structure
static struct driver ns16550_driver = {
    .name = "ns16550_uart",
    .class = DRIVER_CLASS_UART,
    .ops = &ns16550_driver_ops,
    .matches = ns16550_matches,
    .num_matches = sizeof(ns16550_matches) / sizeof(ns16550_matches[0]),
    .priority = 10,
    .priv_size = sizeof(struct uart_softc) + sizeof(struct ns16550_priv),
    .flags = DRIVER_FLAG_BUILTIN,
};

// Driver initialization function
void ns16550_driver_init(void) {
    int ret;
    
    uart_puts("NS16550: Registering driver\n");
    
    ret = driver_register(&ns16550_driver);
    if (ret == 0) {
        uart_puts("NS16550: Driver registered successfully\n");
    } else {
        uart_puts("NS16550: Failed to register driver\n");
    }
}