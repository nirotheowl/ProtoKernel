/*
 * kernel/include/drivers/driver_class.h
 * 
 * Device class-specific abstractions for different driver types
 * Provides specialized structures for UART, Network, Block, etc.
 */

#ifndef __DRIVER_CLASS_H
#define __DRIVER_CLASS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <drivers/driver.h>

// Forward declarations
struct device;
struct mbuf;
struct net_stats;

/*
 * UART Driver Class
 */

// UART capabilities flags
#define UART_CAP_FIFO       (1 << 0)   // Has hardware FIFO
#define UART_CAP_DMA        (1 << 1)   // Supports DMA
#define UART_CAP_AUTOBAUD   (1 << 2)   // Auto-baud detection
#define UART_CAP_MODEM      (1 << 3)   // Modem control lines
#define UART_CAP_IRDA       (1 << 4)   // IrDA support
#define UART_CAP_9BIT       (1 << 5)   // 9-bit mode

// UART parity modes
typedef enum {
    UART_PARITY_NONE,
    UART_PARITY_ODD,
    UART_PARITY_EVEN,
    UART_PARITY_MARK,
    UART_PARITY_SPACE,
} uart_parity_t;

// UART software context
struct uart_softc {
    struct device      *dev;           // Associated device
    const struct uart_class *class;    // UART class operations
    void              *regs;           // Memory-mapped registers
    void              *priv;           // Driver private data
    struct uart_softc *next;           // Next UART in global list
    uint32_t          clock_freq;      // Input clock frequency
    uint32_t          baudrate;        // Current baud rate
    uint32_t          flags;           // Status flags
    uart_parity_t     parity;          // Parity mode
    uint8_t           databits;        // Data bits (5-9)
    uint8_t           stopbits;        // Stop bits (1-2)
    uint8_t           _pad[2];         // Padding for alignment
};

// UART class operations
struct uart_ops {
    int (*init)(struct uart_softc *sc);
    void (*putc)(struct uart_softc *sc, char c);
    int (*getc)(struct uart_softc *sc);
    bool (*readable)(struct uart_softc *sc);
    int (*set_baudrate)(struct uart_softc *sc, uint32_t baud);
    int (*set_format)(struct uart_softc *sc, int databits, int stopbits, uart_parity_t parity);
    void (*enable_irq)(struct uart_softc *sc);
    void (*disable_irq)(struct uart_softc *sc);
    void (*flush)(struct uart_softc *sc);
};

// UART class descriptor
struct uart_class {
    const char          *name;         // Class name
    const struct uart_ops *ops;        // Operations
    size_t              softc_size;    // Size of software context
    uint32_t            capabilities;  // Capability flags
};

/*
 * Network Driver Class
 */

// Network capabilities
#define NET_CAP_CSUM_IP4    (1 << 0)   // IPv4 checksum offload
#define NET_CAP_CSUM_TCP    (1 << 1)   // TCP checksum offload
#define NET_CAP_CSUM_UDP    (1 << 2)   // UDP checksum offload
#define NET_CAP_TSO         (1 << 3)   // TCP segmentation offload
#define NET_CAP_VLAN        (1 << 4)   // VLAN support
#define NET_CAP_JUMBO       (1 << 5)   // Jumbo frames
#define NET_CAP_MULTICAST   (1 << 6)   // Multicast filtering

// Network software context
struct net_softc {
    struct device      *dev;           // Associated device
    const struct net_class *class;     // Network class operations
    void              *regs;           // Memory-mapped registers
    uint8_t           hwaddr[6];       // Hardware MAC address
    uint32_t          mtu;             // Maximum transmission unit
    uint32_t          link_speed;      // Link speed in Mbps
    bool              link_up;         // Link status
    uint32_t          flags;           // Interface flags
    void              *priv;           // Driver private data
};

// Network class operations
struct net_ops {
    int (*init)(struct net_softc *sc);
    int (*open)(struct net_softc *sc);
    int (*close)(struct net_softc *sc);
    int (*send)(struct net_softc *sc, struct mbuf *m);
    int (*recv)(struct net_softc *sc, struct mbuf **m);
    int (*ioctl)(struct net_softc *sc, int cmd, void *data);
    void (*get_hwaddr)(struct net_softc *sc, uint8_t *addr);
    int (*set_hwaddr)(struct net_softc *sc, const uint8_t *addr);
    void (*get_stats)(struct net_softc *sc, struct net_stats *stats);
};

// Network class descriptor
struct net_class {
    const char          *name;         // Class name
    const struct net_ops *ops;         // Operations
    size_t              softc_size;    // Size of software context
    uint32_t            capabilities;  // Capability flags
};

/*
 * Block Device Driver Class
 */

// Block device capabilities
#define BLOCK_CAP_REMOVABLE (1 << 0)   // Removable media
#define BLOCK_CAP_TRIM      (1 << 1)   // TRIM/discard support
#define BLOCK_CAP_FLUSH     (1 << 2)   // Flush cache support
#define BLOCK_CAP_FUA       (1 << 3)   // Force unit access
#define BLOCK_CAP_ROTATIONAL (1 << 4)  // Rotational device (HDD)

// Block device information
struct block_info {
    uint64_t            capacity;      // Total capacity in sectors
    uint32_t            sector_size;   // Sector size in bytes
    uint32_t            max_transfer;  // Max transfer size in sectors
    uint32_t            optimal_io;    // Optimal I/O size in sectors
    char                model[64];     // Device model string
    char                serial[32];    // Serial number
};

// Block software context
struct block_softc {
    struct device      *dev;           // Associated device
    const struct block_class *class;   // Block class operations
    void              *regs;           // Memory-mapped registers
    struct block_info  info;           // Device information
    uint32_t          flags;           // Device flags
    void              *priv;           // Driver private data
};

// Block class operations
struct block_ops {
    int (*init)(struct block_softc *sc);
    int (*read)(struct block_softc *sc, uint64_t lba, void *buf, size_t count);
    int (*write)(struct block_softc *sc, uint64_t lba, const void *buf, size_t count);
    int (*flush)(struct block_softc *sc);
    int (*trim)(struct block_softc *sc, uint64_t lba, size_t count);
    int (*get_info)(struct block_softc *sc, struct block_info *info);
    int (*media_changed)(struct block_softc *sc);
};

// Block class descriptor
struct block_class {
    const char          *name;         // Class name
    const struct block_ops *ops;       // Operations
    size_t              softc_size;    // Size of software context
    uint32_t            capabilities;  // Capability flags
};

/*
 * GPIO Driver Class
 */

// GPIO direction
typedef enum {
    GPIO_DIR_IN,
    GPIO_DIR_OUT,
} gpio_direction_t;

// GPIO pull resistor configuration
typedef enum {
    GPIO_PULL_NONE,
    GPIO_PULL_UP,
    GPIO_PULL_DOWN,
} gpio_pull_t;

// GPIO software context
struct gpio_softc {
    struct device      *dev;           // Associated device
    const struct gpio_class *class;    // GPIO class operations
    void              *regs;           // Memory-mapped registers
    uint32_t          num_pins;        // Number of GPIO pins
    uint32_t          flags;           // Controller flags
    void              *priv;           // Driver private data
};

// GPIO class operations
struct gpio_ops {
    int (*init)(struct gpio_softc *sc);
    int (*set_direction)(struct gpio_softc *sc, uint32_t pin, gpio_direction_t dir);
    int (*set_value)(struct gpio_softc *sc, uint32_t pin, bool value);
    int (*get_value)(struct gpio_softc *sc, uint32_t pin);
    int (*set_pull)(struct gpio_softc *sc, uint32_t pin, gpio_pull_t pull);
    int (*enable_irq)(struct gpio_softc *sc, uint32_t pin, int trigger);
    int (*disable_irq)(struct gpio_softc *sc, uint32_t pin);
};

// GPIO class descriptor
struct gpio_class {
    const char          *name;         // Class name
    const struct gpio_ops *ops;        // Operations
    size_t              softc_size;    // Size of software context
    uint32_t            capabilities;  // Capability flags
};

/*
 * I2C Driver Class
 */

// I2C transfer flags
#define I2C_FLAG_READ       (1 << 0)   // Read transfer
#define I2C_FLAG_WRITE      (0 << 0)   // Write transfer
#define I2C_FLAG_10BIT      (1 << 1)   // 10-bit addressing
#define I2C_FLAG_NOSTART    (1 << 2)   // No START condition
#define I2C_FLAG_NOSTOP     (1 << 3)   // No STOP condition

// I2C message
struct i2c_msg {
    uint16_t            addr;          // Slave address
    uint16_t            flags;         // Transfer flags
    uint16_t            len;           // Message length
    uint8_t            *buf;           // Message buffer
};

// I2C software context
struct i2c_softc {
    struct device      *dev;           // Associated device
    const struct i2c_class *class;     // I2C class operations
    void              *regs;           // Memory-mapped registers
    uint32_t          speed;           // Bus speed in Hz
    uint32_t          flags;           // Controller flags
    void              *priv;           // Driver private data
};

// I2C class operations
struct i2c_ops {
    int (*init)(struct i2c_softc *sc);
    int (*transfer)(struct i2c_softc *sc, struct i2c_msg *msgs, int num);
    int (*set_speed)(struct i2c_softc *sc, uint32_t speed);
    int (*reset)(struct i2c_softc *sc);
};

// I2C class descriptor
struct i2c_class {
    const char          *name;         // Class name
    const struct i2c_ops *ops;         // Operations
    size_t              softc_size;    // Size of software context
    uint32_t            capabilities;  // Capability flags
};

/*
 * Timer Driver Class
 */

// Timer types
typedef enum {
    TIMER_TYPE_ONESHOT,
    TIMER_TYPE_PERIODIC,
} timer_type_t;

// Timer callback
typedef void (*timer_callback_t)(void *arg);

// Timer software context
struct timer_softc {
    struct device      *dev;           // Associated device
    const struct timer_class *class;   // Timer class operations
    void              *regs;           // Memory-mapped registers
    uint32_t          frequency;       // Timer frequency in Hz
    timer_callback_t  callback;        // Timer callback
    void              *callback_arg;   // Callback argument
    uint32_t          flags;           // Timer flags
    void              *priv;           // Driver private data
};

// Timer class operations
struct timer_ops {
    int (*init)(struct timer_softc *sc);
    int (*start)(struct timer_softc *sc, uint64_t nsec, timer_type_t type);
    int (*stop)(struct timer_softc *sc);
    uint64_t (*get_counter)(struct timer_softc *sc);
    int (*set_callback)(struct timer_softc *sc, timer_callback_t cb, void *arg);
};

// Timer class descriptor
struct timer_class {
    const char          *name;         // Class name
    const struct timer_ops *ops;       // Operations
    size_t              softc_size;    // Size of software context
    uint32_t            capabilities;  // Capability flags
};

/*
 * Interrupt Controller Driver Class
 */

// Interrupt trigger types
typedef enum {
    IRQ_TYPE_NONE         = 0,
    IRQ_TYPE_EDGE_RISING  = 1,
    IRQ_TYPE_EDGE_FALLING = 2,
    IRQ_TYPE_EDGE_BOTH    = 3,
    IRQ_TYPE_LEVEL_HIGH   = 4,
    IRQ_TYPE_LEVEL_LOW    = 8,
} irq_type_t;

// Interrupt handler
typedef void (*irq_handler_t)(int irq, void *arg);

// Interrupt controller software context
struct intc_softc {
    struct device      *dev;           // Associated device
    const struct intc_class *class;    // Interrupt controller class
    void              *regs;           // Memory-mapped registers
    uint32_t          num_irqs;        // Number of interrupts
    uint32_t          flags;           // Controller flags
    void              *priv;           // Driver private data
};

// Interrupt controller operations
struct intc_ops {
    int (*init)(struct intc_softc *sc);
    int (*enable)(struct intc_softc *sc, int irq);
    int (*disable)(struct intc_softc *sc, int irq);
    int (*set_type)(struct intc_softc *sc, int irq, irq_type_t type);
    int (*set_priority)(struct intc_softc *sc, int irq, int priority);
    int (*set_affinity)(struct intc_softc *sc, int irq, int cpu);
    void (*eoi)(struct intc_softc *sc, int irq);  // End of interrupt
};

// Interrupt controller class descriptor
struct intc_class {
    const char          *name;         // Class name
    const struct intc_ops *ops;        // Operations
    size_t              softc_size;    // Size of software context
    uint32_t            capabilities;  // Capability flags
};

/*
 * Helper functions for driver classes
 */

// UART helpers
struct uart_softc *uart_device_get_softc(struct device *dev);
int uart_console_attach(struct uart_softc *sc);

// Network helpers
struct net_softc *net_device_get_softc(struct device *dev);

// Block helpers
struct block_softc *block_device_get_softc(struct device *dev);

// Common driver class initialization
void driver_classes_init(void);

#endif // __DRIVER_CLASS_H