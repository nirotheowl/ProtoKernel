/*
 * kernel/device/device_core.c
 * 
 * Core device management implementation
 * Handles device registration, lookup, and tree management
 */

#include <device/device.h>
#include <device/resource.h>
#include <string.h>
#include <uart.h>

/* Forward declarations for device pool functions */
extern struct device *device_pool_alloc_device(void);
extern char *device_pool_strdup(const char *str);

/* Forward declaration for driver system */
struct driver;

/* Device registry structure */
typedef struct {
    struct device   *devices;       /* Head of global device list */
    struct device   *root;          /* Root of device tree */
    uint32_t        count;          /* Total device count */
    uint32_t        next_id;        /* Next device ID to assign */
    bool            initialized;    /* Registry initialized */
} device_registry_t;

/* Global device registry */
static device_registry_t device_registry = {
    .devices = NULL,
    .root = NULL,
    .count = 0,
    .next_id = 1,
    .initialized = false
};

/* Device type names for debugging */
static const char *device_type_names[] = {
    [DEV_TYPE_PLATFORM]     = "platform",
    [DEV_TYPE_PCI]          = "pci",
    [DEV_TYPE_USB]          = "usb",
    [DEV_TYPE_VIRTIO]       = "virtio",
    [DEV_TYPE_CPU]          = "cpu",
    [DEV_TYPE_MEMORY]       = "memory",
    [DEV_TYPE_INTERRUPT]    = "interrupt-controller",
    [DEV_TYPE_TIMER]        = "timer",
    [DEV_TYPE_UART]         = "uart",
    [DEV_TYPE_RTC]          = "rtc",
    [DEV_TYPE_WATCHDOG]     = "watchdog",
    [DEV_TYPE_GPIO]         = "gpio",
    [DEV_TYPE_I2C]          = "i2c",
    [DEV_TYPE_SPI]          = "spi",
    [DEV_TYPE_ETHERNET]     = "ethernet",
    [DEV_TYPE_BLOCK]        = "block",
    [DEV_TYPE_DISPLAY]      = "display",
    [DEV_TYPE_UNKNOWN]      = "unknown"
};

/* Initialize device subsystem */
static bool device_core_init(void) {
    struct device *root;
    
    // uart_puts("DEVICE: device_core_init called\n");
    
    if (device_registry.initialized) {
        // uart_puts("DEVICE: Already initialized\n");
        return true;
    }
    
    /* Device pool must already be initialized by kernel_main */
    
    /* Allocate root device directly to avoid recursion */
    // uart_puts("DEVICE: Allocating root device directly\n");
    root = device_pool_alloc_device();
    if (!root) {
        // uart_puts("DEVICE: Failed to allocate root device\n");
        return false;
    }
    
    // uart_puts("DEVICE: Root device allocated at ");
    // uart_puthex((uint64_t)root);
    // uart_puts("\n");
    
    /* Initialize root device */
    // uart_puts("DEVICE: Initializing root device\n");
    memset(root, 0, sizeof(struct device));
    strncpy(root->name, "root", DEVICE_NAME_MAX - 1);
    root->id = 0;  /* Root gets ID 0 */
    root->type = DEV_TYPE_PLATFORM;
    root->active = true;
    
    /* Set registry fields */
    device_registry.root = root;
    device_registry.next_id = 1;  /* Start regular devices at ID 1 */
    device_registry.initialized = true;
    
    // uart_puts("DEVICE: Core initialized successfully\n");
    
    return true;
}

/* Allocate a new device structure */
struct device *device_alloc(void) {
    struct device *dev;
    
    /* Ensure subsystem is initialized */
    if (!device_registry.initialized && !device_core_init()) {
        return NULL;
    }
    
    /* Allocate from device pool */
    dev = device_pool_alloc_device();
    if (!dev) {
        // uart_puts("DEVICE: Failed to allocate device\n");
        return NULL;
    }
    
    /* Initialize device fields */
    memset(dev, 0, sizeof(struct device));
    dev->id = device_registry.next_id++;
    dev->type = DEV_TYPE_UNKNOWN;
    
    return dev;
}

/* Free a device structure */
void device_free(struct device *dev) {
    if (!dev) {
        return;
    }
    
    /* For now, we don't actually free memory in early boot */
    /* Just mark it as invalid */
    dev->id = 0;
    dev->active = false;
}

/* Register a device */
struct device *device_register(const char *name, device_type_t type) {
    struct device *dev;
    
    if (!name) {
        // uart_puts("DEVICE: Cannot register device without name\n");
        return NULL;
    }
    
    /* Allocate device */
    dev = device_alloc();
    if (!dev) {
        return NULL;
    }
    
    /* Set device properties */
    strncpy(dev->name, name, DEVICE_NAME_MAX - 1);
    dev->name[DEVICE_NAME_MAX - 1] = '\0';
    dev->type = type;
    
    /* Add to global device list */
    dev->next = device_registry.devices;
    device_registry.devices = dev;
    device_registry.count++;
    
    /* If no parent specified, add to root */
    if (!dev->parent && dev != device_registry.root) {
        device_add_child(device_registry.root, dev);
    }
    
    // uart_puts("DEVICE: Registered '");
    // uart_puts(name);
    // uart_puts("' (id=");
    // uart_puthex(dev->id);
    // uart_puts(", type=");
    // uart_puts(device_type_to_string(type));
    // uart_puts(")\n");
    
    return dev;
}

/* Unregister a device */
void device_unregister(struct device *dev) {
    struct device *curr, *prev;
    
    if (!dev) {
        return;
    }
    
    /* Remove from parent's children list */
    if (dev->parent) {
        device_remove_child(dev->parent, dev);
    }
    
    /* Remove from global list */
    prev = NULL;
    curr = device_registry.devices;
    while (curr) {
        if (curr == dev) {
            if (prev) {
                prev->next = curr->next;
            } else {
                device_registry.devices = curr->next;
            }
            device_registry.count--;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    /* Deactivate device */
    dev->active = false;
    
    // uart_puts("DEVICE: Unregistered '");
    // uart_puts(dev->name);
    // uart_puts("'\n");
}

/* Find device by name */
struct device *device_find_by_name(const char *name) {
    struct device *dev;
    
    if (!name) {
        return NULL;
    }
    
    /* Search global device list */
    for (dev = device_registry.devices; dev; dev = dev->next) {
        if (strcmp(dev->name, name) == 0) {
            return dev;
        }
    }
    
    return NULL;
}

/* Find device by compatible string */
struct device *device_find_by_compatible(const char *compatible) {
    struct device *dev;
    
    if (!compatible) {
        return NULL;
    }
    
    /* Search global device list */
    for (dev = device_registry.devices; dev; dev = dev->next) {
        if (dev->compatible && strcmp(dev->compatible, compatible) == 0) {
            return dev;
        }
    }
    
    return NULL;
}

/* Find device by type */
struct device *device_find_by_type(device_type_t type) {
    struct device *dev;
    
    /* Search global device list */
    for (dev = device_registry.devices; dev; dev = dev->next) {
        if (dev->type == type) {
            return dev;
        }
    }
    
    return NULL;
}

/* Find device by ID */
struct device *device_find_by_id(uint32_t id) {
    struct device *dev;
    
    /* Search global device list */
    for (dev = device_registry.devices; dev; dev = dev->next) {
        if (dev->id == id) {
            return dev;
        }
    }
    
    return NULL;
}

/* Add child to parent device */
void device_add_child(struct device *parent, struct device *child) {
    if (!parent || !child) {
        return;
    }
    
    /* Remove from current parent if any */
    if (child->parent) {
        device_remove_child(child->parent, child);
    }
    
    /* Set new parent */
    child->parent = parent;
    
    /* Add to parent's children list (at head) */
    child->sibling = parent->children;
    parent->children = child;
}

/* Remove child from parent device */
void device_remove_child(struct device *parent, struct device *child) {
    struct device *curr, *prev;
    
    if (!parent || !child || child->parent != parent) {
        return;
    }
    
    /* Find and remove from children list */
    prev = NULL;
    curr = parent->children;
    while (curr) {
        if (curr == child) {
            if (prev) {
                prev->sibling = curr->sibling;
            } else {
                parent->children = curr->sibling;
            }
            child->parent = NULL;
            child->sibling = NULL;
            break;
        }
        prev = curr;
        curr = curr->sibling;
    }
}

/* Get child device by name */
struct device *device_get_child(struct device *parent, const char *name) {
    struct device *child;
    
    if (!parent || !name) {
        return NULL;
    }
    
    /* Search children */
    for (child = parent->children; child; child = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
    }
    
    return NULL;
}

/* Get next child in iteration */
struct device *device_get_next_child(struct device *parent, struct device *child) {
    if (!parent) {
        return NULL;
    }
    
    if (!child) {
        return parent->children;
    }
    
    return child->sibling;
}

/* Iterate over all children */
int device_for_each_child(struct device *parent, 
                         int (*callback)(struct device *, void *), 
                         void *data) {
    struct device *child, *next;
    int ret;
    
    if (!parent || !callback) {
        return -1;
    }
    
    /* Iterate safely in case callback modifies the tree */
    child = parent->children;
    while (child) {
        next = child->sibling;
        ret = callback(child, data);
        if (ret != 0) {
            return ret;
        }
        child = next;
    }
    
    return 0;
}


/* Get root device */
struct device *device_get_root(void) {
    // uart_puts("DEVICE: device_get_root called\n");
    // uart_puts("DEVICE: device_registry.initialized = ");
    // uart_puthex(device_registry.initialized);
    // uart_puts("\n");
    
    if (!device_registry.initialized) {
        // uart_puts("DEVICE: Not initialized, calling device_core_init\n");
        if (!device_core_init()) {
            // uart_puts("DEVICE: device_core_init failed\n");
            return NULL;
        }
    }
    
    // uart_puts("DEVICE: Returning root device at ");
    // uart_puthex((uint64_t)device_registry.root);
    // uart_puts("\n");
    
    return device_registry.root;
}

/* Device property functions */
bool device_set_name(struct device *dev, const char *name) {
    if (!dev || !name) {
        return false;
    }
    
    strncpy(dev->name, name, DEVICE_NAME_MAX - 1);
    dev->name[DEVICE_NAME_MAX - 1] = '\0';
    return true;
}

const char *device_get_name(struct device *dev) {
    return dev ? dev->name : NULL;
}

device_type_t device_get_type(struct device *dev) {
    return dev ? dev->type : DEV_TYPE_UNKNOWN;
}

void device_set_driver_data(struct device *dev, void *data) {
    if (dev) {
        dev->driver_data = data;
    }
}

void *device_get_driver_data(struct device *dev) {
    return dev ? dev->driver_data : NULL;
}

const char *device_get_compatible(struct device *dev) {
    return dev ? dev->compatible : NULL;
}

void device_set_compatible(struct device *dev, const char *compatible) {
    if (dev) {
        dev->compatible = compatible;
    }
}

/* Device state management */
bool device_activate(struct device *dev) {
    if (!dev) {
        return false;
    }
    
    dev->active = true;
    return true;
}

bool device_deactivate(struct device *dev) {
    if (!dev) {
        return false;
    }
    
    dev->active = false;
    return true;
}

bool device_suspend(struct device *dev) {
    if (!dev) {
        return false;
    }
    
    dev->suspended = true;
    return true;
}

bool device_resume(struct device *dev) {
    if (!dev) {
        return false;
    }
    
    dev->suspended = false;
    return true;
}

bool device_is_active(struct device *dev) {
    return dev ? dev->active : false;
}

/* Get total device count */
uint32_t device_count_total(void) {
    return device_registry.count;
}

/* Convert device type to string */
const char *device_type_to_string(device_type_t type) {
    if (type >= 0 && type < sizeof(device_type_names) / sizeof(device_type_names[0])) {
        return device_type_names[type];
    }
    return "invalid";
}

/* Convert string to device type */
device_type_t device_string_to_type(const char *str) {
    size_t i;
    
    if (!str) {
        return DEV_TYPE_UNKNOWN;
    }
    
    for (i = 0; i < sizeof(device_type_names) / sizeof(device_type_names[0]); i++) {
        if (device_type_names[i] && strcmp(str, device_type_names[i]) == 0) {
            return (device_type_t)i;
        }
    }
    
    return DEV_TYPE_UNKNOWN;
}

/* Resource management for devices */
int device_get_clock_freq(struct device *dev, int index) {
    /* TODO: Implement clock frequency extraction from FDT */
    (void)dev;
    (void)index;
    return 0;
}

int device_register_fdt(struct device *dev, int fdt_offset) {
    if (!dev) {
        return -1;
    }
    
    dev->fdt_offset = fdt_offset;
    
    /* Add to global device list */
    dev->next = device_registry.devices;
    device_registry.devices = dev;
    device_registry.count++;
    
    return 0;
}

/* Print device information */
void device_print_info(struct device *dev) {
    if (!dev) {
        return;
    }
    
    uart_puts("Device: ");
    uart_puts(dev->name);
    uart_puts("\n");
    uart_puts("  ID:         ");
    // uart_puthex(dev->id);
    uart_puts("\n");
    uart_puts("  Type:       ");
    uart_puts(device_type_to_string(dev->type));
    uart_puts("\n");
    uart_puts("  Status:     ");
    if (dev->active) {
        uart_puts("active");
        if (dev->suspended) {
            uart_puts(" (suspended)");
        }
    } else {
        uart_puts("inactive");
    }
    uart_puts("\n");
    
    if (dev->compatible) {
        uart_puts("  Compatible: ");
        uart_puts(dev->compatible);
        uart_puts("\n");
    }
    
    uart_puts("  Resources:  ");
    uart_puthex(dev->num_resources);
    uart_puts("\n");
    
    /* Print each resource */
    for (int i = 0; i < dev->num_resources; i++) {
        struct resource *res = &dev->resources[i];
        uart_puts("    ");
        if (res->name) {
            uart_puts(res->name);
        } else {
            uart_puts("unnamed");
        }
        uart_puts(": ");
        
        switch (res->type) {
            case RES_TYPE_MEM:
                uart_puts("MEM ");
                uart_puthex(res->start);
                uart_puts("-");
                uart_puthex(res->end);
                if (res->mapped_addr) {
                    uart_puts(" => ");
                    uart_puthex((uint64_t)res->mapped_addr);
                }
                break;
            case RES_TYPE_IRQ:
                uart_puts("IRQ ");
                uart_puthex(res->start);
                break;
            default:
                uart_puts("??? ");
                uart_puthex(res->start);
                break;
        }
        uart_puts("\n");
    }
    
    if (dev->parent) {
        uart_puts("  Parent:     ");
        uart_puts(dev->parent->name);
        uart_puts("\n");
    }
    
    if (dev->driver) {
        uart_puts("  Driver:     bound\n");
    }
}

/* Print device tree recursively */
void device_print_tree(struct device *root, int indent) {
    struct device *child;
    int i;
    
    if (!root) {
        root = device_registry.root;
        if (!root) {
            // uart_puts("DEVICE: No device tree\n");
            return;
        }
    }
    
    /* Print indentation */
    for (i = 0; i < indent; i++) {
        uart_puts("  ");
    }
    
    /* Print device info */
    uart_puts(root->name);
    uart_puts(" [");
    uart_puts(device_type_to_string(root->type));
    uart_puts("]");
    if (!root->active) {
        uart_puts(" (inactive)");
    }
    uart_puts("\n");
    
    /* Print children */
    for (child = root->children; child; child = child->sibling) {
        device_print_tree(child, indent + 1);
    }
}

/* Initialize device subsystem */
int device_init(void *fdt) {
    int device_count = 0;
    
    /* Initialize device pool - requires PMM to be ready */
    extern bool device_pool_init(void);
    if (!device_pool_init()) {
        uart_puts("ERROR: Failed to initialize device pool\n");
        return -1;
    }
    
    /* Initialize device tree parser with FDT */
    extern int device_tree_init(void *fdt);
    if (device_tree_init(fdt) < 0) {
        uart_puts("ERROR: Failed to initialize device tree subsystem\n");
        return -1;
    }
    
    /* Enumerate all devices from FDT */
    extern int device_tree_populate_devices(void);
    device_count = device_tree_populate_devices();
    if (device_count < 0) {
        uart_puts("ERROR: Failed to enumerate devices\n");
        return -1;
    }
    
    // uart_puts("Device subsystem initialized. Found ");
    // uart_puthex(device_count);
    // uart_puts(" devices.\n");
    
    return device_count;
}