/*
 * kernel/include/drivers/driver.h
 * 
 * Core driver infrastructure for device driver management
 * Provides driver registration, matching, and binding mechanisms
 */

#ifndef __DRIVER_H
#define __DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
struct device;
struct driver;

// Match types for device-driver binding
typedef enum {
    MATCH_COMPATIBLE,      // FDT compatible string
    MATCH_DEVICE_ID,       // PCI/USB vendor:device ID
    MATCH_ACPI_HID,        // ACPI hardware ID
    MATCH_CLASS,           // Device class matching
    MATCH_PROPERTY,        // Custom FDT property
    MATCH_PLATFORM_DATA,   // Legacy platform data
} match_type_t;

// Driver classes for categorization
typedef enum {
    DRIVER_CLASS_NONE,
    DRIVER_CLASS_UART,
    DRIVER_CLASS_NET,
    DRIVER_CLASS_BLOCK,
    DRIVER_CLASS_GPIO,
    DRIVER_CLASS_I2C,
    DRIVER_CLASS_SPI,
    DRIVER_CLASS_PCI,
    DRIVER_CLASS_USB,
    DRIVER_CLASS_MMC,
    DRIVER_CLASS_DISPLAY,
    DRIVER_CLASS_SOUND,
    DRIVER_CLASS_INPUT,
    DRIVER_CLASS_RTC,
    DRIVER_CLASS_WATCHDOG,
    DRIVER_CLASS_TIMER,
    DRIVER_CLASS_INTC,     // Interrupt controller
    DRIVER_CLASS_DMA,
    DRIVER_CLASS_POWER,
    DRIVER_CLASS_THERMAL,
    DRIVER_CLASS_MISC,
} driver_class_t;

// Device match entry for driver matching
struct device_match {
    match_type_t        type;          // Type of match
    const char         *value;         // Match value (string)
    void               *driver_data;   // Optional driver-specific data
};

// Driver operations
struct driver_ops {
    // Probe device for compatibility (returns match score 0-100)
    int (*probe)(struct device *dev);
    
    // Attach driver to device (returns 0 on success, negative on error)
    int (*attach)(struct device *dev);
    
    // Detach driver from device (returns 0 on success)
    int (*detach)(struct device *dev);
    
    // Power management operations (optional)
    int (*suspend)(struct device *dev);
    int (*resume)(struct device *dev);
    
    // Device-specific operations (optional)
    int (*ioctl)(struct device *dev, int cmd, void *arg);
};

// Driver structure
struct driver {
    // Identification
    const char              *name;         // Driver name
    driver_class_t          class;         // Driver class
    
    // Operations
    const struct driver_ops *ops;          // Driver operations
    
    // Matching
    const struct device_match *matches;    // Array of match criteria
    size_t                  num_matches;   // Number of match entries
    int                     priority;      // Base priority (lower = higher)
    
    // Private data
    size_t                  priv_size;     // Size of driver private data
    
    // Registry management (internal use)
    struct driver          *next;          // Next in registry
    uint32_t               flags;          // Driver flags
    uint32_t               ref_count;      // Reference count
};

// Driver flags
#define DRIVER_FLAG_REGISTERED  (1 << 0)   // Driver is registered
#define DRIVER_FLAG_BUILTIN     (1 << 1)   // Built-in driver (not module)
#define DRIVER_FLAG_EARLY       (1 << 2)   // Early driver (console, etc.)
#define DRIVER_FLAG_DISABLED    (1 << 3)   // Driver is disabled

// Probe return values and scoring
#define PROBE_SCORE_EXACT       100         // Exact match
#define PROBE_SCORE_FAMILY      90          // Same family/variant
#define PROBE_SCORE_VENDOR      75          // Vendor-specific generic
#define PROBE_SCORE_GENERIC     50          // Generic/compatible
#define PROBE_SCORE_CLASS       25          // Class-based fallback
#define PROBE_SCORE_WILDCARD    10          // Wildcard match
#define PROBE_SCORE_NONE        0           // No match
#define PROBE_SCORE_ERROR       (-1)        // Probe error

// Minimum score required for driver binding
#define PROBE_THRESHOLD         10

// Driver API functions

// Driver registration
int driver_register(struct driver *drv);
int driver_unregister(struct driver *drv);

// Driver lookup
struct driver *driver_find_by_name(const char *name);
struct driver *driver_find_by_class(driver_class_t class);
struct driver *driver_get_next(struct driver *drv);

// Device-driver binding
int driver_probe_device(struct device *dev);
int driver_attach_device(struct device *dev, struct driver *drv);
int driver_detach_device(struct device *dev);

// Driver matching helpers
int driver_match_compatible(struct driver *drv, const char *compatible);
int driver_match_device_id(struct driver *drv, const char *device_id);
int driver_match_property(struct driver *drv, const char *prop, const char *value);

// Driver statistics and debugging
uint32_t driver_count_registered(void);
void driver_print_registry(void);
void driver_print_info(struct driver *drv);

// Driver class helpers
const char *driver_class_to_string(driver_class_t class);
driver_class_t driver_string_to_class(const char *str);

// Early driver support (for console, etc.)
struct early_driver {
    const char *compatible;
    int (*init)(uintptr_t base, uint32_t size);
    void (*putc)(char c);
};

// Macro for declaring early drivers
#define EARLY_DRIVER(name, compat, init_fn, putc_fn) \
    static struct early_driver __early_##name \
    __attribute__((section(".early_drivers"), used)) = { \
        .compatible = compat, \
        .init = init_fn, \
        .putc = putc_fn \
    }

// Initialize driver subsystem
int driver_init(void);

// Module-like macros for built-in drivers
#define module_driver_init(initfn) \
    static void __attribute__((constructor)) initfn##_module(void) { \
        initfn(); \
    }

#endif // __DRIVER_H