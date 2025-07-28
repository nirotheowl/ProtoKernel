/*
 * kernel/include/device/device.h
 * 
 * Core device structures and management
 * Provides the foundation for device enumeration and driver binding
 */

#ifndef __DEVICE_H
#define __DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Device limits and constants */
#define DEVICE_NAME_MAX         64
#define DEVICE_MAX_RESOURCES    8
#define DEVICE_MAX_COUNT        1024

/* Forward declarations */
struct driver;
struct resource;

/* Device types */
typedef enum {
    DEV_TYPE_PLATFORM,      /* Standard platform device */
    DEV_TYPE_PCI,           /* PCI/PCIe device */
    DEV_TYPE_USB,           /* USB device */
    DEV_TYPE_VIRTIO,        /* VirtIO device */
    DEV_TYPE_CPU,           /* CPU core */
    DEV_TYPE_MEMORY,        /* Memory controller */
    DEV_TYPE_INTERRUPT,     /* Interrupt controller */
    DEV_TYPE_TIMER,         /* Timer device */
    DEV_TYPE_UART,          /* UART serial port */
    DEV_TYPE_RTC,           /* Real-time clock */
    DEV_TYPE_WATCHDOG,      /* Watchdog timer */
    DEV_TYPE_GPIO,          /* GPIO controller */
    DEV_TYPE_I2C,           /* I2C controller */
    DEV_TYPE_SPI,           /* SPI controller */
    DEV_TYPE_ETHERNET,      /* Ethernet controller */
    DEV_TYPE_BLOCK,         /* Block device */
    DEV_TYPE_DISPLAY,       /* Display controller */
    DEV_TYPE_UNKNOWN
} device_type_t;

/* Device structure */
struct device {
    /* Identification */
    uint32_t            id;                         /* Unique device ID */
    char                name[DEVICE_NAME_MAX];      /* Device name from FDT */
    device_type_t       type;                       /* Device type */
    
    /* FDT Information */
    uint32_t            fdt_offset;                 /* Offset in FDT blob */
    const char          *compatible;                /* Compatible string (points to FDT) */
    
    /* Resources */
    struct resource     *resources;                 /* Array of resources */
    uint8_t             num_resources;              /* Number of resources */
    
    /* Driver binding */
    struct driver       *driver;                    /* Bound driver (if any) */
    void                *driver_data;               /* Driver private data */
    
    /* Device tree hierarchy */
    struct device       *parent;                    /* Parent device */
    struct device       *children;                  /* First child */
    struct device       *sibling;                   /* Next sibling */
    
    /* Global list */
    struct device       *next;                      /* Next in global device list */
    
    /* Status and flags */
    bool                active;                     /* Device is active/probed */
    bool                suspended;                  /* Device is suspended */
    uint32_t            flags;                      /* Device-specific flags */
};

/* Device flags */
#define DEVICE_FLAG_REMOVABLE   (1 << 0)   /* Device can be hot-removed */
#define DEVICE_FLAG_DISABLED    (1 << 1)   /* Device is disabled */
#define DEVICE_FLAG_BOOT        (1 << 2)   /* Boot device */
#define DEVICE_FLAG_DMA_CAPABLE (1 << 3)   /* Device supports DMA */

/* Device API functions */

/* Device registration and management */
struct device *device_register(const char *name, device_type_t type);
void device_unregister(struct device *dev);
struct device *device_alloc(void);
void device_free(struct device *dev);

/* Device lookup functions */
struct device *device_find_by_name(const char *name);
struct device *device_find_by_compatible(const char *compatible);
struct device *device_find_by_type(device_type_t type);
struct device *device_find_by_id(uint32_t id);

/* Device tree navigation */
struct device *device_get_child(struct device *parent, const char *name);
struct device *device_get_next_child(struct device *parent, struct device *child);
int device_for_each_child(struct device *parent, 
                         int (*callback)(struct device *, void *), 
                         void *data);

/* Device properties */
bool device_set_name(struct device *dev, const char *name);
const char *device_get_name(struct device *dev);
device_type_t device_get_type(struct device *dev);
void device_set_driver_data(struct device *dev, void *data);
void *device_get_driver_data(struct device *dev);

/* Device state management */
bool device_activate(struct device *dev);
bool device_deactivate(struct device *dev);
bool device_suspend(struct device *dev);
bool device_resume(struct device *dev);
bool device_is_active(struct device *dev);

/* Device tree management */
void device_add_child(struct device *parent, struct device *child);
void device_remove_child(struct device *parent, struct device *child);
struct device *device_get_root(void);

/* Debug functions */
void device_print_tree(struct device *root, int indent);
void device_print_info(struct device *dev);
uint32_t device_count_total(void);

/* Type conversion helpers */
const char *device_type_to_string(device_type_t type);
device_type_t device_string_to_type(const char *str);

#endif /* __DEVICE_H */