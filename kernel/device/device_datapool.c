/*
 * kernel/device/device_datapool.c
 * 
 * Permanent device data pool using PMM
 * Migrates devices from early pool to permanent memory after PMM init
 */

#include <device/device.h>
#include <device/resource.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <memory/vmparam.h>
#include <string.h>
#include <uart.h>

/* Forward declarations */
extern struct device *device_get_registry_head(void);
extern void device_set_registry_head(struct device *head);
extern uint32_t device_count_total(void);

/* Permanent storage info */
static void *permanent_base = NULL;
static size_t permanent_size = 0;
static size_t permanent_used = 0;
static bool migration_complete = false;

/* Helper to allocate from permanent storage */
static void *permanent_alloc(size_t size) {
    size_t aligned_size;
    void *ptr;
    
    if (!permanent_base) {
        return NULL;
    }
    
    /* Align to 16 bytes */
    aligned_size = (size + 15) & ~15;
    
    if (permanent_used + aligned_size > permanent_size) {
        uart_puts("DEVICE_DATAPOOL: Out of permanent storage\n");
        return NULL;
    }
    
    ptr = (uint8_t *)permanent_base + permanent_used;
    permanent_used += aligned_size;
    
    return ptr;
}

/* Deep copy a string to permanent storage */
static char *copy_string(const char *str) {
    char *new_str;
    size_t len;
    
    if (!str) {
        return NULL;
    }
    
    len = strlen(str) + 1;
    new_str = permanent_alloc(len);
    if (new_str) {
        memcpy(new_str, str, len);
    }
    
    return new_str;
}

/* Note: Resources are now copied inline in copy_device() */

/* Copy a device and all its resources to permanent storage */
static struct device *copy_device(struct device *src) {
    struct device *dst;
    int i;
    
    if (!src) {
        return NULL;
    }
    
    dst = permanent_alloc(sizeof(struct device));
    if (!dst) {
        return NULL;
    }
    
    /* Copy the device structure field by field to avoid memcpy call */
    dst->id = src->id;
    dst->type = src->type;
    dst->fdt_offset = src->fdt_offset;
    dst->num_resources = src->num_resources;
    dst->driver = src->driver;
    dst->driver_data = src->driver_data;
    dst->active = src->active;
    
    /* Deep copy the name */
    if (src->name[0]) {
        strncpy(dst->name, src->name, DEVICE_NAME_MAX - 1);
        dst->name[DEVICE_NAME_MAX - 1] = '\0';
    }
    
    /* Deep copy the compatible string */
    if (src->compatible) {
        dst->compatible = copy_string(src->compatible);
    }
    
    /* Copy all resources if any */
    if (src->num_resources > 0 && src->resources) {
        /* Allocate space for resources array */
        dst->resources = permanent_alloc(src->num_resources * sizeof(struct resource));
        if (dst->resources) {
            /* Copy each resource */
            for (i = 0; i < src->num_resources; i++) {
                dst->resources[i] = src->resources[i];
                /* Deep copy resource name if needed */
                if (src->resources[i].name) {
                    dst->resources[i].name = copy_string(src->resources[i].name);
                }
            }
        } else {
            dst->num_resources = 0;
        }
    } else {
        dst->resources = NULL;
        dst->num_resources = 0;
    }
    
    /* Clear pointers that will be fixed up later */
    dst->parent = NULL;
    dst->children = NULL;
    dst->sibling = NULL;
    dst->next = NULL;
    
    return dst;
}

/* Fix up parent/child/sibling pointers after migration */
static void fixup_device_pointers(struct device *old_dev, struct device *new_dev) {
    struct device *child, *new_child;
    
    if (!old_dev || !new_dev) {
        return;
    }
    
    /* Process children */
    child = old_dev->children;
    while (child) {
        /* Find corresponding new device */
        new_child = device_find_by_name(child->name);
        if (new_child && new_child != child) {
            /* This is the migrated version */
            if (!new_dev->children) {
                new_dev->children = new_child;
            } else {
                /* Find end of sibling list */
                struct device *sibling = new_dev->children;
                while (sibling->sibling) {
                    sibling = sibling->sibling;
                }
                sibling->sibling = new_child;
            }
            new_child->parent = new_dev;
        }
        child = child->sibling;
    }
}

/* Migrate all devices from early pool to permanent storage */
int device_migrate_to_permanent(void) {
    struct device *old_dev, *new_dev, *new_head = NULL, *new_tail = NULL;
    struct device *old_root = device_get_root();
    size_t device_count = device_count_total();
    size_t estimated_size;
    uint64_t phys_addr;
    int migrated = 0;
    
    if (migration_complete) {
        return 0;  /* Already migrated */
    }
    
    uart_puts("\nDEVICE_DATAPOOL: Starting device migration to permanent storage\n");
    
    /* Estimate required size */
    estimated_size = device_count * (sizeof(struct device) + 256);  /* Device + strings */
    estimated_size += device_count * 8 * sizeof(struct resource);   /* Resources */
    estimated_size = (estimated_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  /* Page align */
    
    uart_puts("DEVICE_DATAPOOL: Estimated size: ");
    uart_puthex(estimated_size);
    uart_puts(" bytes for ");
    uart_puthex(device_count);
    uart_puts(" devices\n");
    
    /* Allocate permanent storage from PMM */
    phys_addr = pmm_alloc_pages(estimated_size / PAGE_SIZE);
    if (!phys_addr) {
        uart_puts("DEVICE_DATAPOOL: Failed to allocate permanent storage\n");
        return -1;
    }
    
    /* Map to kernel virtual space */
    permanent_base = (void *)(KERNEL_VIRT_BASE + 0x1000000);  /* 16MB after kernel */
    if (!vmm_map_range(vmm_get_kernel_context(), 
                       (uint64_t)permanent_base, 
                       phys_addr, 
                       estimated_size,
                       VMM_ATTR_READ | VMM_ATTR_WRITE)) {
        uart_puts("DEVICE_DATAPOOL: Failed to map permanent storage\n");
        pmm_free_pages(phys_addr, estimated_size / PAGE_SIZE);
        return -1;
    }
    
    permanent_size = estimated_size;
    permanent_used = 0;
    
    /* Migrate all devices */
    old_dev = device_get_registry_head();
    while (old_dev) {
        new_dev = copy_device(old_dev);
        if (!new_dev) {
            uart_puts("DEVICE_DATAPOOL: Failed to copy device\n");
            break;
        }
        
        /* Update registry pointers */
        if (!new_head) {
            new_head = new_dev;
            new_tail = new_dev;
        } else {
            new_tail->next = new_dev;
            new_tail = new_dev;
        }
        
        /* Special case for root device */
        if (old_dev == old_root) {
            /* Update root through device_core API if needed */
        }
        
        migrated++;
        old_dev = old_dev->next;
    }
    
    /* Update global registry head */
    device_set_registry_head(new_head);
    
    /* Fix up parent/child/sibling pointers */
    old_dev = device_get_registry_head();
    new_dev = new_head;
    while (old_dev && new_dev) {
        fixup_device_pointers(old_dev, new_dev);
        old_dev = old_dev->next;
        new_dev = new_dev->next;
    }
    
    migration_complete = true;
    
    uart_puts("DEVICE_DATAPOOL: Migration complete. Migrated ");
    uart_puthex(migrated);
    uart_puts(" devices using ");
    uart_puthex(permanent_used);
    uart_puts(" bytes\n");
    
    /* The early pool can now be reclaimed by PMM if desired */
    extern void early_pool_print_stats(void);
    early_pool_print_stats();
    
    return migrated;
}

/* Check if migration is complete */
bool device_migration_complete(void) {
    return migration_complete;
}

/* Get permanent storage statistics */
void device_datapool_stats(void) {
    if (!permanent_base) {
        uart_puts("DEVICE_DATAPOOL: Not initialized\n");
        return;
    }
    
    uart_puts("\nDevice Data Pool Statistics:\n");
    uart_puts("Base address: ");
    uart_puthex((uint64_t)permanent_base);
    uart_puts("\nTotal size:   ");
    uart_puthex(permanent_size);
    uart_puts(" bytes\nUsed:         ");
    uart_puthex(permanent_used);
    uart_puts(" bytes (");
    uart_puthex((permanent_used * 100) / permanent_size);
    uart_puts("%)\nFree:         ");
    uart_puthex(permanent_size - permanent_used);
    uart_puts(" bytes\n");
}