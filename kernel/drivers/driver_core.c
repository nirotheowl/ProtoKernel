/*
 * kernel/drivers/driver_core.c
 * 
 * Core driver infrastructure implementation
 * Manages driver registration, matching, and device binding
 */

#include <drivers/driver.h>
#include <device/device.h>
#include <device/resource.h>
#include <string.h>
#include <memory/kmalloc.h>
#include <memory/slab.h>
#include <panic.h>
#include <uart.h>

// Driver registry head
static struct driver *driver_registry = NULL;

// Statistics
static uint32_t driver_count = 0;

// Lock for registry protection (will use spinlock when available)
// static spinlock_t driver_lock = SPINLOCK_INIT;

// Helper function to find a match entry in driver's match table
static const struct device_match *find_match_entry(struct driver *drv, match_type_t type, const char *value) {
    if (!drv || !drv->matches || !value) {
        return NULL;
    }
    
    for (size_t i = 0; i < drv->num_matches; i++) {
        const struct device_match *match = &drv->matches[i];
        if (match->type == type && match->value) {
            if (strcmp(match->value, value) == 0) {
                return match;
            }
        }
    }
    
    return NULL;
}

static int calculate_compatible_score(struct driver *drv, const char *compatible) {
    const struct device_match *match;
    
    if (!compatible) {
        return PROBE_SCORE_NONE;
    }
    
    // Look for exact match
    match = find_match_entry(drv, MATCH_COMPATIBLE, compatible);
    if (match) {
        return PROBE_SCORE_EXACT;
    }
    
    // Check for partial matches (vendor prefix, family name, etc.)
    for (size_t i = 0; i < drv->num_matches; i++) {
        const struct device_match *m = &drv->matches[i];
        if (m->type == MATCH_COMPATIBLE && m->value) {
            // Check if compatible contains the match value
            if (strstr(compatible, m->value)) {
                return PROBE_SCORE_GENERIC;
            }
            
            // Check vendor prefix match (e.g., "arm,*" matches "arm,pl011")
            const char *comma1 = strchr(compatible, ',');
            const char *comma2 = strchr(m->value, ',');
            if (comma1 && comma2) {
                size_t vendor_len = comma1 - compatible;
                if (vendor_len == (size_t)(comma2 - m->value) &&
                    strncmp(compatible, m->value, vendor_len) == 0) {
                    return PROBE_SCORE_VENDOR;
                }
            }
        }
    }
    
    return PROBE_SCORE_NONE;
}

int driver_register(struct driver *drv) {
    if (!drv || !drv->name || !drv->ops) {
        return -1;
    }
    
    // Check if already registered
    if (drv->flags & DRIVER_FLAG_REGISTERED) {
        return -1;
    }
    
    // Validate operations
    if (!drv->ops->probe || !drv->ops->attach) {
        panic("Driver %s missing required operations", drv->name);
        return -1;
    }
    
    // Check for duplicate name
    if (driver_find_by_name(drv->name)) {
        panic("Driver %s already registered", drv->name);
        return -1;
    }
    
    // Add to registry (at head for simplicity)
    drv->next = driver_registry;
    driver_registry = drv;
    
    // Set flags and counters
    drv->flags |= DRIVER_FLAG_REGISTERED;
    drv->ref_count = 0;
    driver_count++;
    
    // Try to bind to any unbound devices
    struct device *dev = device_get_root();
    while (dev) {
        if (!dev->driver) {
            driver_probe_device(dev);
        }
        dev = dev->next;
    }
    
    return 0;
}

int driver_unregister(struct driver *drv) {
    struct driver **pp;
    
    if (!drv || !(drv->flags & DRIVER_FLAG_REGISTERED)) {
        return -1;
    }
    
    // Check reference count
    if (drv->ref_count > 0) {
        return -1;  // Still in use
    }
    
    // Find and remove from registry
    for (pp = &driver_registry; *pp; pp = &(*pp)->next) {
        if (*pp == drv) {
            *pp = drv->next;
            drv->next = NULL;
            drv->flags &= ~DRIVER_FLAG_REGISTERED;
            driver_count--;
            
            // Detach from any devices
            struct device *dev = device_get_root();
            while (dev) {
                if (dev->driver == drv) {
                    driver_detach_device(dev);
                }
                dev = dev->next;
            }
            
            return 0;
        }
    }
    
    return -1;  // Not found
}

struct driver *driver_find_by_name(const char *name) {
    struct driver *drv;
    
    if (!name) {
        return NULL;
    }
    
    for (drv = driver_registry; drv; drv = drv->next) {
        if (strcmp(drv->name, name) == 0) {
            return drv;
        }
    }
    
    return NULL;
}

struct driver *driver_find_by_class(driver_class_t class) {
    struct driver *drv;
    
    for (drv = driver_registry; drv; drv = drv->next) {
        if (drv->class == class) {
            return drv;
        }
    }
    
    return NULL;
}

// Get next driver in registry
struct driver *driver_get_next(struct driver *drv) {
    if (!drv) {
        return driver_registry;
    }
    return drv->next;
}

// Probe device for best matching driver
int driver_probe_device(struct device *dev) {
    struct driver *drv, *best_driver = NULL;
    int score, best_score = 0;
    
    if (!dev) {
        return -1;
    }
    
    // Device already has a driver
    if (dev->driver) {
        return 0;
    }
    
    // Try each registered driver
    for (drv = driver_registry; drv; drv = drv->next) {
        // Skip disabled drivers
        if (drv->flags & DRIVER_FLAG_DISABLED) {
            continue;
        }
        
        // Check if driver class matches device type hint
        if (dev->type != DEV_TYPE_UNKNOWN) {
            // Map device type to driver class (simplified)
            driver_class_t expected_class = DRIVER_CLASS_NONE;
            switch (dev->type) {
                case DEV_TYPE_UART:
                    expected_class = DRIVER_CLASS_UART;
                    break;
                case DEV_TYPE_ETHERNET:
                    expected_class = DRIVER_CLASS_NET;
                    break;
                case DEV_TYPE_BLOCK:
                    expected_class = DRIVER_CLASS_BLOCK;
                    break;
                case DEV_TYPE_GPIO:
                    expected_class = DRIVER_CLASS_GPIO;
                    break;
                case DEV_TYPE_I2C:
                    expected_class = DRIVER_CLASS_I2C;
                    break;
                case DEV_TYPE_SPI:
                    expected_class = DRIVER_CLASS_SPI;
                    break;
                case DEV_TYPE_TIMER:
                    expected_class = DRIVER_CLASS_TIMER;
                    break;
                case DEV_TYPE_INTERRUPT:
                    expected_class = DRIVER_CLASS_INTC;
                    break;
                case DEV_TYPE_RTC:
                    expected_class = DRIVER_CLASS_RTC;
                    break;
                case DEV_TYPE_WATCHDOG:
                    expected_class = DRIVER_CLASS_WATCHDOG;
                    break;
                default:
                    break;
            }
            
            if (expected_class != DRIVER_CLASS_NONE && 
                drv->class != expected_class && 
                drv->class != DRIVER_CLASS_MISC) {
                continue;
            }
        }
        
        // Call driver's probe function
        score = drv->ops->probe(dev);
        
        // Add driver priority to score
        if (score > 0) {
            score += drv->priority;
        }
        
        // Track best match
        if (score > best_score) {
            best_score = score;
            best_driver = drv;
        }
        
        // Perfect match found, no need to continue
        if (score >= PROBE_SCORE_EXACT) {
            break;
        }
    }
    
    // Attach best driver if score meets threshold
    if (best_driver && best_score >= PROBE_THRESHOLD) {
        return driver_attach_device(dev, best_driver);
    }
    
    return -1;  // No suitable driver found
}

int driver_attach_device(struct device *dev, struct driver *drv) {
    int ret;
    
    if (!dev || !drv) {
        return -1;
    }
    
    // Device already has a driver
    if (dev->driver) {
        return -1;
    }
    
    // Allocate driver private data if needed
    if (drv->priv_size > 0) {
        dev->driver_data = kmalloc(drv->priv_size, KM_ZERO);
        if (!dev->driver_data) {
            return -1;
        }
    }
    
    // Call driver's attach function
    ret = drv->ops->attach(dev);
    if (ret != 0) {
        // Attach failed, cleanup
        if (dev->driver_data) {
            kfree(dev->driver_data);
            dev->driver_data = NULL;
        }
        return ret;
    }
    
    // Success - bind driver to device
    dev->driver = drv;
    drv->ref_count++;
    dev->active = true;
    
    return 0;
}

int driver_detach_device(struct device *dev) {
    struct driver *drv;
    int ret = 0;
    
    if (!dev || !dev->driver) {
        return -1;
    }
    
    drv = dev->driver;
    
    // Call driver's detach function if provided
    if (drv->ops->detach) {
        ret = drv->ops->detach(dev);
        if (ret != 0) {
            return ret;  // Detach refused
        }
    }
    
    // Cleanup
    if (dev->driver_data) {
        kfree(dev->driver_data);
        dev->driver_data = NULL;
    }
    
    // Unbind
    dev->driver = NULL;
    dev->active = false;
    drv->ref_count--;
    
    return 0;
}

// Match driver against compatible string
int driver_match_compatible(struct driver *drv, const char *compatible) {
    if (!drv || !compatible) {
        return 0;
    }
    
    return calculate_compatible_score(drv, compatible);
}

// Match driver against device ID
int driver_match_device_id(struct driver *drv, const char *device_id) {
    if (!drv || !device_id) {
        return 0;
    }
    
    if (find_match_entry(drv, MATCH_DEVICE_ID, device_id)) {
        return PROBE_SCORE_EXACT;
    }
    
    return PROBE_SCORE_NONE;
}

// Match driver against property
int driver_match_property(struct driver *drv, const char *prop, const char *value) {
    if (!drv || !prop || !value) {
        return 0;
    }
    
    // For now, simple string match on property value
    for (size_t i = 0; i < drv->num_matches; i++) {
        const struct device_match *match = &drv->matches[i];
        if (match->type == MATCH_PROPERTY && match->value) {
            // Format: "property=value"
            char buf[256];
            snprintf(buf, sizeof(buf), "%s=%s", prop, value);
            if (strcmp(match->value, buf) == 0) {
                return PROBE_SCORE_EXACT;
            }
        }
    }
    
    return PROBE_SCORE_NONE;
}

uint32_t driver_count_registered(void) {
    return driver_count;
}

void driver_print_registry(void) {
    struct driver *drv;
    
    uart_puts("Driver Registry:\n");
    uart_puts("----------------\n");
    
    for (drv = driver_registry; drv; drv = drv->next) {
        uart_puts("  ");
        uart_puts(drv->name);
        uart_puts(" [");
        uart_puts(driver_class_to_string(drv->class));
        uart_puts("] refs=");
        uart_putdec(drv->ref_count);
        
        if (drv->flags & DRIVER_FLAG_DISABLED) {
            uart_puts(" DISABLED");
        }
        if (drv->flags & DRIVER_FLAG_EARLY) {
            uart_puts(" EARLY");
        }
        
        uart_puts("\n");
    }
    
    uart_puts("Total drivers: ");
    uart_putdec(driver_count);
    uart_puts("\n");
}

void driver_print_info(struct driver *drv) {
    if (!drv) {
        return;
    }
    
    uart_puts("Driver: ");
    uart_puts(drv->name);
    uart_puts("\n");
    uart_puts("  Class: ");
    uart_puts(driver_class_to_string(drv->class));
    uart_puts("\n");
    uart_puts("  Priority: ");
    uart_putdec(drv->priority);
    uart_puts("\n");
    uart_puts("  References: ");
    uart_putdec(drv->ref_count);
    uart_puts("\n");
    uart_puts("  Matches: ");
    uart_putdec(drv->num_matches);
    uart_puts("\n");
    
    for (size_t i = 0; i < drv->num_matches; i++) {
        const struct device_match *match = &drv->matches[i];
        uart_puts("    ");
        
        switch (match->type) {
            case MATCH_COMPATIBLE:
                uart_puts("compatible: ");
                break;
            case MATCH_DEVICE_ID:
                uart_puts("device_id: ");
                break;
            case MATCH_ACPI_HID:
                uart_puts("acpi_hid: ");
                break;
            case MATCH_CLASS:
                uart_puts("class: ");
                break;
            case MATCH_PROPERTY:
                uart_puts("property: ");
                break;
            case MATCH_PLATFORM_DATA:
                uart_puts("platform: ");
                break;
        }
        
        if (match->value) {
            uart_puts(match->value);
        }
        uart_puts("\n");
    }
}

// Convert driver class to string
const char *driver_class_to_string(driver_class_t class) {
    switch (class) {
        case DRIVER_CLASS_NONE:     return "none";
        case DRIVER_CLASS_UART:     return "uart";
        case DRIVER_CLASS_NET:      return "network";
        case DRIVER_CLASS_BLOCK:    return "block";
        case DRIVER_CLASS_GPIO:     return "gpio";
        case DRIVER_CLASS_I2C:      return "i2c";
        case DRIVER_CLASS_SPI:      return "spi";
        case DRIVER_CLASS_PCI:      return "pci";
        case DRIVER_CLASS_USB:      return "usb";
        case DRIVER_CLASS_MMC:      return "mmc";
        case DRIVER_CLASS_DISPLAY:  return "display";
        case DRIVER_CLASS_SOUND:    return "sound";
        case DRIVER_CLASS_INPUT:    return "input";
        case DRIVER_CLASS_RTC:      return "rtc";
        case DRIVER_CLASS_WATCHDOG: return "watchdog";
        case DRIVER_CLASS_TIMER:    return "timer";
        case DRIVER_CLASS_INTC:     return "intc";
        case DRIVER_CLASS_DMA:      return "dma";
        case DRIVER_CLASS_POWER:    return "power";
        case DRIVER_CLASS_THERMAL:  return "thermal";
        case DRIVER_CLASS_MISC:     return "misc";
        default:                    return "unknown";
    }
}

// Convert string to driver class
driver_class_t driver_string_to_class(const char *str) {
    if (!str) return DRIVER_CLASS_NONE;
    
    if (strcmp(str, "uart") == 0)     return DRIVER_CLASS_UART;
    if (strcmp(str, "network") == 0)  return DRIVER_CLASS_NET;
    if (strcmp(str, "block") == 0)    return DRIVER_CLASS_BLOCK;
    if (strcmp(str, "gpio") == 0)     return DRIVER_CLASS_GPIO;
    if (strcmp(str, "i2c") == 0)      return DRIVER_CLASS_I2C;
    if (strcmp(str, "spi") == 0)      return DRIVER_CLASS_SPI;
    if (strcmp(str, "pci") == 0)      return DRIVER_CLASS_PCI;
    if (strcmp(str, "usb") == 0)      return DRIVER_CLASS_USB;
    if (strcmp(str, "mmc") == 0)      return DRIVER_CLASS_MMC;
    if (strcmp(str, "display") == 0)  return DRIVER_CLASS_DISPLAY;
    if (strcmp(str, "sound") == 0)    return DRIVER_CLASS_SOUND;
    if (strcmp(str, "input") == 0)    return DRIVER_CLASS_INPUT;
    if (strcmp(str, "rtc") == 0)      return DRIVER_CLASS_RTC;
    if (strcmp(str, "watchdog") == 0) return DRIVER_CLASS_WATCHDOG;
    if (strcmp(str, "timer") == 0)    return DRIVER_CLASS_TIMER;
    if (strcmp(str, "intc") == 0)     return DRIVER_CLASS_INTC;
    if (strcmp(str, "dma") == 0)      return DRIVER_CLASS_DMA;
    if (strcmp(str, "power") == 0)    return DRIVER_CLASS_POWER;
    if (strcmp(str, "thermal") == 0)  return DRIVER_CLASS_THERMAL;
    if (strcmp(str, "misc") == 0)     return DRIVER_CLASS_MISC;
    
    return DRIVER_CLASS_NONE;
}

// Initialize driver subsystem
int driver_init(void) {
    driver_registry = NULL;
    driver_count = 0;
    
    uart_puts("Driver subsystem initialized\n");
    
    return 0;
}
