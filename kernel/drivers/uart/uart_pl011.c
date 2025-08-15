/*
 * kernel/drivers/uart/uart_pl011.c
 * 
 * ARM PL011 UART driver
 */

#include <drivers/driver.h>
#include <drivers/driver_class.h>
#include <drivers/uart_drivers.h>
#include <drivers/driver_module.h>
#include <device/device.h>
#include <device/resource.h>
#include <uart.h>
#include <string.h>
#include <memory/kmalloc.h>
#include <panic.h>

// PL011 UART registers (offsets from base)
#define PL011_DR        0x00    // Data register
#define PL011_RSR       0x04    // Receive status register / Error clear register
#define PL011_FR        0x18    // Flag register
#define PL011_ILPR      0x20    // IrDA low-power counter register
#define PL011_IBRD      0x24    // Integer baud rate divisor
#define PL011_FBRD      0x28    // Fractional baud rate divisor
#define PL011_LCR_H     0x2C    // Line control register
#define PL011_CR        0x30    // Control register
#define PL011_IFLS      0x34    // Interrupt FIFO level select
#define PL011_IMSC      0x38    // Interrupt mask set/clear
#define PL011_RIS       0x3C    // Raw interrupt status
#define PL011_MIS       0x40    // Masked interrupt status
#define PL011_ICR       0x44    // Interrupt clear register
#define PL011_DMACR     0x48    // DMA control register

// Flag register bits
#define PL011_FR_CTS    (1 << 0)   // Clear to send
#define PL011_FR_DSR    (1 << 1)   // Data set ready
#define PL011_FR_DCD    (1 << 2)   // Data carrier detect
#define PL011_FR_BUSY   (1 << 3)   // UART busy
#define PL011_FR_RXFE   (1 << 4)   // Receive FIFO empty
#define PL011_FR_TXFF   (1 << 5)   // Transmit FIFO full
#define PL011_FR_RXFF   (1 << 6)   // Receive FIFO full
#define PL011_FR_TXFE   (1 << 7)   // Transmit FIFO empty
#define PL011_FR_RI     (1 << 8)   // Ring indicator

// Control register bits
#define PL011_CR_UARTEN     (1 << 0)   // UART enable
#define PL011_CR_SIREN      (1 << 1)   // SIR enable
#define PL011_CR_SIRLP      (1 << 2)   // SIR low-power mode
#define PL011_CR_LBE        (1 << 7)   // Loopback enable
#define PL011_CR_TXE        (1 << 8)   // Transmit enable
#define PL011_CR_RXE        (1 << 9)   // Receive enable
#define PL011_CR_DTR        (1 << 10)  // Data transmit ready
#define PL011_CR_RTS        (1 << 11)  // Request to send
#define PL011_CR_OUT1       (1 << 12)  // Output 1
#define PL011_CR_OUT2       (1 << 13)  // Output 2
#define PL011_CR_RTSEN      (1 << 14)  // RTS hardware flow control enable
#define PL011_CR_CTSEN      (1 << 15)  // CTS hardware flow control enable

// Line control register bits
#define PL011_LCR_H_BRK     (1 << 0)   // Send break
#define PL011_LCR_H_PEN     (1 << 1)   // Parity enable
#define PL011_LCR_H_EPS     (1 << 2)   // Even parity select
#define PL011_LCR_H_STP2    (1 << 3)   // Two stop bits select
#define PL011_LCR_H_FEN     (1 << 4)   // Enable FIFOs
#define PL011_LCR_H_WLEN_5  (0 << 5)   // Word length 5 bits
#define PL011_LCR_H_WLEN_6  (1 << 5)   // Word length 6 bits
#define PL011_LCR_H_WLEN_7  (2 << 5)   // Word length 7 bits
#define PL011_LCR_H_WLEN_8  (3 << 5)   // Word length 8 bits
#define PL011_LCR_H_SPS     (1 << 7)   // Stick parity select

// Interrupt bits
#define PL011_INT_RIM       (1 << 0)   // Ring indicator
#define PL011_INT_CTSM      (1 << 1)   // CTS modem
#define PL011_INT_DCDM      (1 << 2)   // DCD modem
#define PL011_INT_DSRM      (1 << 3)   // DSR modem
#define PL011_INT_RX        (1 << 4)   // Receive
#define PL011_INT_TX        (1 << 5)   // Transmit
#define PL011_INT_RT        (1 << 6)   // Receive timeout
#define PL011_INT_FE        (1 << 7)   // Framing error
#define PL011_INT_PE        (1 << 8)   // Parity error
#define PL011_INT_BE        (1 << 9)   // Break error
#define PL011_INT_OE        (1 << 10)  // Overrun error

// PL011 driver private data
struct pl011_priv {
    bool initialized;       // Driver initialized flag
};

// Forward declaration
static int pl011_set_baudrate(struct uart_softc *sc, uint32_t baud);

// Device match table
static struct device_match pl011_matches[] = {
    { MATCH_COMPATIBLE, "arm,pl011", NULL },
    { MATCH_COMPATIBLE, "arm,primecell", NULL },
};

// UART operations implementation
static int pl011_init(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Disable UART during configuration
    regs[PL011_CR/4] = 0;
    
    // Clear pending interrupts
    regs[PL011_ICR/4] = 0x7FF;
    
    // Disable interrupts for now
    regs[PL011_IMSC/4] = 0;
    
    // Enable FIFOs
    regs[PL011_LCR_H/4] |= PL011_LCR_H_FEN;
    
    // Set default format: 8N1
    regs[PL011_LCR_H/4] = PL011_LCR_H_WLEN_8 | PL011_LCR_H_FEN;
    
    // Set default baud rate (115200)
    pl011_set_baudrate(sc, 115200);
    
    // Enable UART, TX, and RX
    regs[PL011_CR/4] = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
    
    return 0;
}

static void pl011_putc(struct uart_softc *sc, char c) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Wait for transmit FIFO to have space
    while (regs[PL011_FR/4] & PL011_FR_TXFF) {
        // Spin
    }
    
    // Write character
    regs[PL011_DR/4] = c;
}

static int pl011_getc(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Check if receive FIFO is empty
    if (regs[PL011_FR/4] & PL011_FR_RXFE) {
        return -1;
    }
    
    // Read character
    return regs[PL011_DR/4] & 0xFF;
}

static bool pl011_readable(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    return !(regs[PL011_FR/4] & PL011_FR_RXFE);
}

static int pl011_set_baudrate(struct uart_softc *sc, uint32_t baud) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    uint32_t ibrd, fbrd;
    uint32_t temp;
    uint32_t cr;
    
    if (baud == 0) {
        return -1;
    }
    
    // Calculate divisors
    // Baud rate divisor = UARTCLK / (16 * Baud rate)
    // IBRD = integer part
    // FBRD = fractional part * 64 + 0.5
    temp = sc->clock_freq * 4 / baud;
    ibrd = temp / 64;
    fbrd = temp % 64;
    
    if (ibrd == 0 || ibrd > 0xFFFF) {
        return -1;
    }
    
    // Save control register
    cr = regs[PL011_CR/4];
    
    // Disable UART
    regs[PL011_CR/4] = 0;
    
    // Wait for UART to be not busy
    while (regs[PL011_FR/4] & PL011_FR_BUSY) {
        // Spin
    }
    
    // Set divisors
    regs[PL011_IBRD/4] = ibrd;
    regs[PL011_FBRD/4] = fbrd;
    
    // Restore control register
    regs[PL011_CR/4] = cr;
    
    return 0;
}

static int pl011_set_format(struct uart_softc *sc, int databits, int stopbits, 
                            uart_parity_t parity)
{
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    uint32_t lcr = 0;
    uint32_t cr;
    
    // Set data bits
    switch (databits) {
        case 5: lcr |= PL011_LCR_H_WLEN_5; break;
        case 6: lcr |= PL011_LCR_H_WLEN_6; break;
        case 7: lcr |= PL011_LCR_H_WLEN_7; break;
        case 8: lcr |= PL011_LCR_H_WLEN_8; break;
        default: return -1;
    }
    
    // Set stop bits
    if (stopbits == 2) {
        lcr |= PL011_LCR_H_STP2;
    } else if (stopbits != 1) {
        return -1;
    }
    
    // Set parity
    switch (parity) {
        case UART_PARITY_NONE:
            break;
        case UART_PARITY_ODD:
            lcr |= PL011_LCR_H_PEN;
            break;
        case UART_PARITY_EVEN:
            lcr |= PL011_LCR_H_PEN | PL011_LCR_H_EPS;
            break;
        case UART_PARITY_MARK:
            lcr |= PL011_LCR_H_PEN | PL011_LCR_H_SPS;
            break;
        case UART_PARITY_SPACE:
            lcr |= PL011_LCR_H_PEN | PL011_LCR_H_EPS | PL011_LCR_H_SPS;
            break;
        default:
            return -1;
    }
    
    // Keep FIFOs enabled
    lcr |= PL011_LCR_H_FEN;
    
    // Save control register
    cr = regs[PL011_CR/4];
    
    // Disable UART
    regs[PL011_CR/4] = 0;
    
    // Wait for UART to be not busy
    while (regs[PL011_FR/4] & PL011_FR_BUSY) {
        // Spin
    }
    
    // Write line control register
    regs[PL011_LCR_H/4] = lcr;
    
    // Restore control register
    regs[PL011_CR/4] = cr;
    
    return 0;
}

static void pl011_enable_irq(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Enable receive and receive timeout interrupts
    regs[PL011_IMSC/4] = PL011_INT_RX | PL011_INT_RT;
}

static void pl011_disable_irq(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Disable all interrupts
    regs[PL011_IMSC/4] = 0;
    
    // Clear pending interrupts
    regs[PL011_ICR/4] = 0x7FF;
}

static void pl011_flush(struct uart_softc *sc) {
    volatile uint32_t *regs = (volatile uint32_t *)sc->regs;
    
    // Wait for transmit FIFO to be empty and UART not busy
    while (!(regs[PL011_FR/4] & PL011_FR_TXFE) || 
           (regs[PL011_FR/4] & PL011_FR_BUSY)) {
        // Spin
    }
}

// UART class operations
static struct uart_ops pl011_uart_ops = {
    .init = pl011_init,
    .putc = pl011_putc,
    .getc = pl011_getc,
    .readable = pl011_readable,
    .set_baudrate = pl011_set_baudrate,
    .set_format = pl011_set_format,
    .enable_irq = pl011_enable_irq,
    .disable_irq = pl011_disable_irq,
    .flush = pl011_flush,
};

// UART class descriptor
static struct uart_class pl011_uart_class = {
    .name = "pl011",
    .ops = &pl011_uart_ops,
    .softc_size = sizeof(struct uart_softc),
    .capabilities = UART_CAP_FIFO | UART_CAP_MODEM,
};

static int pl011_probe(struct device *dev) {
    const char *compat;
    
    // Get device compatible string
    compat = device_get_compatible(dev);
    if (!compat) {
        return PROBE_SCORE_NONE;
    }
    
    // Check for exact PL011 match
    if (strstr(compat, "arm,pl011")) {
        return PROBE_SCORE_EXACT;
    }
    
    // Check for generic primecell (could be PL011)
    if (strstr(compat, "arm,primecell")) {
        // Could check AMBA ID registers here to confirm
        return PROBE_SCORE_GENERIC;
    }
    
    return PROBE_SCORE_NONE;
}

static int pl011_attach(struct device *dev) {
    struct uart_softc *sc;
    struct pl011_priv *priv;
    struct resource *res;
    volatile uint32_t *regs;
    
    uart_puts("PL011: Attaching to device ");
    uart_puts(device_get_name(dev));
    uart_puts("\n");
    
    // Allocate software context
    sc = kmalloc(sizeof(*sc) + sizeof(*priv), KM_ZERO);
    if (!sc) {
        return -1;
    }
    priv = (struct pl011_priv *)(sc + 1);
    
    // Initialize UART software context
    uart_softc_init(sc, dev, &pl011_uart_class);
    
    // Set priv pointer AFTER uart_softc_init
    sc->priv = priv;
    
    // Check if device is mapped
    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res || !res->mapped_addr) {
        uart_puts("PL011: Device not mapped\n");
        kfree(sc);
        return -1;
    }
    
    regs = (volatile uint32_t *)sc->regs;
    
    // Check if UART is already enabled by bootloader
    if (regs[PL011_CR/4] & PL011_CR_UARTEN) {
        uart_puts("PL011: UART already enabled by bootloader\n");
        priv->initialized = true;
        
        // Just clear any pending interrupts
        regs[PL011_ICR/4] = 0x7FF;
        regs[PL011_IMSC/4] = 0;
    } else {
        // Initialize hardware
        if (pl011_init(sc) != 0) {
            uart_puts("PL011: Failed to initialize hardware\n");
            kfree(sc);
            return -1;
        }
        priv->initialized = true;
    }
    
    // Store context in device
    device_set_driver_data(dev, sc);
    
    // Mark device as active
    device_activate(dev);
    
    uart_puts("PL011: Device attached successfully at ");
    uart_puthex((uint64_t)sc->regs);
    uart_puts("\n");
    
    return 0;
}

static int pl011_detach(struct device *dev) {
    struct uart_softc *sc;
    struct pl011_priv *priv;
    
    uart_puts("PL011: Detaching from device ");
    uart_puts(device_get_name(dev));
    uart_puts("\n");
    
    // Get software context
    sc = device_get_driver_data(dev);
    if (sc) {
        priv = (struct pl011_priv *)sc->priv;
        
        if (priv && priv->initialized) {
            // Disable interrupts
            pl011_disable_irq(sc);
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

static struct driver_ops pl011_driver_ops = {
    .probe = pl011_probe,
    .attach = pl011_attach,
    .detach = pl011_detach,
    .suspend = NULL,
    .resume = NULL,
    .ioctl = NULL,
};

static struct driver pl011_driver = {
    .name = "pl011_uart",
    .class = DRIVER_CLASS_UART,
    .ops = &pl011_driver_ops,
    .matches = pl011_matches,
    .num_matches = sizeof(pl011_matches) / sizeof(pl011_matches[0]),
    .priority = 10,
    .priv_size = sizeof(struct uart_softc) + sizeof(struct pl011_priv),
    .flags = DRIVER_FLAG_BUILTIN | DRIVER_FLAG_EARLY,
};

// NOTE: This is the *driver* init 
static void pl011_driver_init(void) {
    int ret;
    
    uart_puts("PL011: Registering driver\n");
    
    ret = driver_register(&pl011_driver);
    if (ret == 0) {
        uart_puts("PL011: Driver registered successfully\n");
    } else {
        uart_puts("PL011: Failed to register driver\n");
    }
}

UART_DRIVER_MODULE(pl011_driver_init, DRIVER_PRIO_NORMAL);
