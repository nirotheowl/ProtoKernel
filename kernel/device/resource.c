/*
 * kernel/device/resource.c
 * 
 * Device resource management implementation
 * Handles memory regions, interrupts, DMA channels, etc.
 */

#include <device/device.h>
#include <device/resource.h>
#include <string.h>
#include <uart.h>

/* Forward declarations for device pool functions */
extern struct resource *device_pool_alloc_resource(void);
extern char *device_pool_strdup(const char *str);
extern void *device_pool_alloc(size_t size);

/* Resource type names for debugging */
static const char *resource_type_names[] = {
    [RES_TYPE_MEM]      = "memory",
    [RES_TYPE_IO]       = "io",
    [RES_TYPE_IRQ]      = "irq",
    [RES_TYPE_DMA]      = "dma",
    [RES_TYPE_BUS]      = "bus",
    [RES_TYPE_CLOCK]    = "clock",
    [RES_TYPE_POWER]    = "power",
    [RES_TYPE_RESET]    = "reset"
};

/* Global system resources (defined but not initialized in early boot) */
struct resource system_mem_resource = {
    .type = RES_TYPE_MEM,
    .name = "System RAM",
    .start = 0,
    .end = 0,
    .flags = RES_MEM_CACHEABLE
};

struct resource system_io_resource = {
    .type = RES_TYPE_IO,
    .name = "System I/O",
    .start = 0,
    .end = 0,
    .flags = 0
};

/* Allocate a new resource structure */
struct resource *resource_alloc(void) {
    struct resource *res;
    
    /* Allocate from device pool */
    res = device_pool_alloc_resource();
    if (!res) {
        uart_puts("RESOURCE: Failed to allocate resource\n");
        return NULL;
    }
    
    /* Initialize resource fields */
    memset(res, 0, sizeof(struct resource));
    
    return res;
}

/* Free a resource structure */
void resource_free(struct resource *res) {
    if (!res) {
        return;
    }
    
    /* For now, we don't actually free memory in early boot */
    /* Just clear the structure */
    memset(res, 0, sizeof(struct resource));
}

/* Initialize a resource */
bool resource_init(struct resource *res, resource_type_t type,
                  uint64_t start, uint64_t end, const char *name) {
    if (!res) {
        return false;
    }
    
    res->type = type;
    res->start = start;
    res->end = end;
    res->flags = 0;
    res->name = name;
    res->mapped_addr = NULL;
    res->parent = NULL;
    res->child = NULL;
    res->sibling = NULL;
    
    return true;
}

/* Add a resource to a device */
int device_add_resource(struct device *dev, struct resource *res) {
    struct resource *dev_res;
    
    if (!dev || !res) {
        return -1;
    }
    
    /* Check if we have space for more resources */
    if (dev->num_resources >= DEVICE_MAX_RESOURCES) {
        uart_puts("RESOURCE: Device '");
        uart_puts(dev->name);
        uart_puts("' has too many resources\n");
        return -1;
    }
    
    /* Allocate resource array if needed */
    if (!dev->resources) {
        dev->resources = (struct resource *)device_pool_alloc(
            DEVICE_MAX_RESOURCES * sizeof(struct resource));
        if (!dev->resources) {
            uart_puts("RESOURCE: Failed to allocate resource array\n");
            return -1;
        }
        memset(dev->resources, 0, DEVICE_MAX_RESOURCES * sizeof(struct resource));
    }
    
    /* Copy resource to device's array */
    dev_res = &dev->resources[dev->num_resources];
    memcpy(dev_res, res, sizeof(struct resource));
    
    /* Duplicate name if provided */
    if (res->name) {
        dev_res->name = device_pool_strdup(res->name);
    }
    
    dev->num_resources++;
    
    return 0;
}

/* Add a memory resource to a device */
int device_add_mem_resource(struct device *dev, uint64_t start, uint64_t size,
                           uint64_t flags, const char *name) {
    struct resource res;
    
    if (!dev || size == 0) {
        return -1;
    }
    
    /* Initialize resource */
    resource_init(&res, RES_TYPE_MEM, start, start + size - 1, name);
    res.flags = flags;
    
    return device_add_resource(dev, &res);
}

/* Add an IRQ resource to a device */
int device_add_irq_resource(struct device *dev, uint32_t irq, uint64_t flags,
                           const char *name) {
    struct resource res;
    
    if (!dev) {
        return -1;
    }
    
    /* Initialize resource */
    resource_init(&res, RES_TYPE_IRQ, irq, irq, name);
    res.flags = flags;
    
    return device_add_resource(dev, &res);
}

/* Get a resource from a device by type and index */
struct resource *device_get_resource(struct device *dev, resource_type_t type,
                                   int index) {
    int count = 0;
    int i;
    
    if (!dev || !dev->resources || index < 0) {
        return NULL;
    }
    
    /* Find the nth resource of the requested type */
    for (i = 0; i < dev->num_resources; i++) {
        if (dev->resources[i].type == type) {
            if (count == index) {
                return &dev->resources[i];
            }
            count++;
        }
    }
    
    return NULL;
}

/* Get a resource from a device by type and name */
struct resource *device_get_resource_by_name(struct device *dev,
                                           resource_type_t type,
                                           const char *name) {
    int i;
    
    if (!dev || !dev->resources || !name) {
        return NULL;
    }
    
    /* Find resource by type and name */
    for (i = 0; i < dev->num_resources; i++) {
        if (dev->resources[i].type == type &&
            dev->resources[i].name &&
            strcmp(dev->resources[i].name, name) == 0) {
            return &dev->resources[i];
        }
    }
    
    return NULL;
}

/* Get resource size */
uint64_t resource_size(const struct resource *res) {
    if (!res) {
        return 0;
    }
    
    /* Handle wraparound for full address space */
    if (res->end < res->start) {
        return 0;
    }
    
    return res->end - res->start + 1;
}

/* Check if one resource contains another */
bool resource_contains(const struct resource *parent, const struct resource *child) {
    if (!parent || !child) {
        return false;
    }
    
    return parent->start <= child->start && parent->end >= child->end;
}

/* Check if two resources overlap */
bool resource_overlaps(const struct resource *r1, const struct resource *r2) {
    if (!r1 || !r2) {
        return false;
    }
    
    /* Different resource types don't overlap */
    if (r1->type != r2->type) {
        return false;
    }
    
    /* Check for overlap */
    return r1->start <= r2->end && r2->start <= r1->end;
}

/* Check if resource is valid */
bool resource_is_valid(const struct resource *res) {
    if (!res) {
        return false;
    }
    
    /* Check type is valid */
    if (res->type >= RES_TYPE_MAX) {
        return false;
    }
    
    /* Check range is valid */
    if (res->start > res->end) {
        return false;
    }
    
    return true;
}

/* Add a child resource to a parent */
int resource_add_child(struct resource *parent, struct resource *child) {
    struct resource *sibling;
    
    if (!parent || !child) {
        return -1;
    }
    
    /* Check if child fits within parent */
    if (!resource_contains(parent, child)) {
        uart_puts("RESOURCE: Child resource doesn't fit in parent\n");
        return -1;
    }
    
    /* Check for conflicts with existing children */
    for (sibling = parent->child; sibling; sibling = sibling->sibling) {
        if (resource_overlaps(sibling, child)) {
            uart_puts("RESOURCE: Child resource conflicts with sibling\n");
            return -1;
        }
    }
    
    /* Add to parent's children list */
    child->parent = parent;
    child->sibling = parent->child;
    parent->child = child;
    
    return 0;
}

/* Remove a child resource from a parent */
int resource_remove_child(struct resource *parent, struct resource *child) {
    struct resource *curr, *prev;
    
    if (!parent || !child || child->parent != parent) {
        return -1;
    }
    
    /* Find and remove from children list */
    prev = NULL;
    curr = parent->child;
    while (curr) {
        if (curr == child) {
            if (prev) {
                prev->sibling = curr->sibling;
            } else {
                parent->child = curr->sibling;
            }
            child->parent = NULL;
            child->sibling = NULL;
            return 0;
        }
        prev = curr;
        curr = curr->sibling;
    }
    
    return -1;
}

/* Find a child resource by range */
struct resource *resource_find_child(struct resource *parent, uint64_t start,
                                   uint64_t end) {
    struct resource *child;
    
    if (!parent) {
        return NULL;
    }
    
    /* Search children for exact match */
    for (child = parent->child; child; child = child->sibling) {
        if (child->start == start && child->end == end) {
            return child;
        }
    }
    
    return NULL;
}

/* Check if resource is MMIO */
bool resource_is_mmio(const struct resource *res) {
    return res && res->type == RES_TYPE_MEM;
}

/* Get mapped virtual address for resource */
void *resource_get_mapped_addr(struct resource *res) {
    if (!res || res->type != RES_TYPE_MEM) {
        return NULL;
    }
    return res->mapped_addr;
}

/* Set mapped virtual address for resource */
int resource_set_mapped_addr(struct resource *res, void *vaddr) {
    if (!res || res->type != RES_TYPE_MEM) {
        return -1;
    }
    res->mapped_addr = vaddr;
    return 0;
}

/* Check if IRQ resource is valid */
bool resource_is_irq_valid(const struct resource *res) {
    return res && res->type == RES_TYPE_IRQ && res->start <= 1024;
}

/* Get IRQ number from resource */
uint32_t resource_irq_get_number(const struct resource *res) {
    if (!resource_is_irq_valid(res)) {
        return 0;
    }
    return (uint32_t)res->start;
}

/* Get IRQ flags from resource */
uint64_t resource_irq_get_flags(const struct resource *res) {
    if (!resource_is_irq_valid(res)) {
        return 0;
    }
    return res->flags;
}

/* Convert resource type to string */
const char *resource_type_to_string(resource_type_t type) {
    if (type >= 0 && type < sizeof(resource_type_names) / sizeof(resource_type_names[0])) {
        return resource_type_names[type];
    }
    return "invalid";
}

/* Print resource information */
void resource_print(const struct resource *res) {
    if (!res) {
        return;
    }
    
    uart_puts("  ");
    uart_puts(resource_type_to_string(res->type));
    uart_puts(": ");
    
    if (res->name) {
        uart_puts(res->name);
        uart_puts(" ");
    }
    
    uart_puts("[");
    uart_puthex(res->start);
    
    if (res->type == RES_TYPE_MEM || res->type == RES_TYPE_IO) {
        uart_puts("-");
        uart_puthex(res->end);
        uart_puts("] (");
        uart_puthex(resource_size(res));
        uart_puts(" bytes)");
    } else if (res->type == RES_TYPE_IRQ) {
        uart_puts("]");
        if (res->flags & RES_IRQ_EDGE) {
            uart_puts(" edge");
        }
        if (res->flags & RES_IRQ_LEVEL) {
            uart_puts(" level");
        }
        if (res->flags & RES_IRQ_SHARED) {
            uart_puts(" shared");
        }
    } else {
        uart_puts("]");
    }
    
    if (res->flags && res->type == RES_TYPE_MEM) {
        uart_puts(" flags:");
        if (res->flags & RES_MEM_CACHEABLE) uart_puts(" cacheable");
        if (res->flags & RES_MEM_PREFETCH) uart_puts(" prefetch");
        if (res->flags & RES_MEM_32BIT) uart_puts(" 32bit");
        if (res->flags & RES_MEM_64BIT) uart_puts(" 64bit");
    }
    
    uart_puts("\n");
}

/* Print resource tree recursively */
void resource_print_tree(const struct resource *root, int indent) {
    const struct resource *child;
    int i;
    
    if (!root) {
        return;
    }
    
    /* Print indentation */
    for (i = 0; i < indent; i++) {
        uart_puts("  ");
    }
    
    /* Print resource */
    resource_print(root);
    
    /* Print children */
    for (child = root->child; child; child = child->sibling) {
        resource_print_tree(child, indent + 1);
    }
}