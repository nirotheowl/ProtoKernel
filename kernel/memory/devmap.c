/*
 * kernel/memory/devmap.c
 *
 * Device memory mapping infrastructure
 * Manages platform-specific device mappings in kernel virtual address space
 */

#include <memory/devmap.h>
#include <memory/vmm.h>
#include <memory/vmparam.h>
#include <memory/pmm.h>
#include <uart.h>
#include <string.h>
#include <stddef.h>
#include <device/device.h>
#include <device/resource.h>

/* Device mapping configuration */
#define DEVMAP_MAX_ENTRIES      512
#define DEVMAP_VA_START         0xFFFF000100000000UL  /* Start after kernel space */
#define DEVMAP_VA_END           0xFFFF000200000000UL  /* 1TB for device mappings */

/* Device mapping table entry */
struct devmap_table_entry {
    devmap_entry_t entry;
    uint64_t allocated_va;
    bool in_use;
};

/* Device mapping table - dynamically allocated */
static struct devmap_table_entry *devmap_table = NULL;
static int devmap_count = 0;
static uint64_t devmap_next_va = DEVMAP_VA_START;
static bool devmap_initialized = false;

/* Current platform */
static const platform_desc_t *current_platform = NULL;

/* Forward declarations */
extern const platform_desc_t qemu_virt_platform;
extern const platform_desc_t odroid_m2_platform;
static int devmap_map_device_tree(struct device *dev);

/* Platform list - add new platforms here */
static const platform_desc_t *platforms[] = {
    &qemu_virt_platform,
    &odroid_m2_platform,
    NULL
};

/* Initialize device mapping system */
void devmap_init(void)
{
    if (devmap_initialized) {
        return;
    }

    /* Allocate device mapping table */
    size_t table_size = sizeof(struct devmap_table_entry) * DEVMAP_MAX_ENTRIES;
    size_t pages_needed = (table_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t phys_addr = pmm_alloc_pages(pages_needed);
    
    if (phys_addr == 0) {
        uart_puts("DEVMAP: Failed to allocate memory for device table\n");
        return;
    }
    
    /* Debug: Check DMAP range */
    uart_puts("DEVMAP: Allocated phys addr: ");
    uart_puthex(phys_addr);
    uart_puts("\n");
    uart_puts("DEVMAP: DMAP range: ");
    uart_puthex(dmap_phys_base);
    uart_puts(" - ");
    uart_puthex(dmap_phys_max);
    uart_puts("\n");
    
    /* Map the allocated pages using DMAP */
    devmap_table = (struct devmap_table_entry *)PHYS_TO_DMAP(phys_addr);
    
    uart_puts("DEVMAP: Virtual addr: ");
    uart_puthex((uint64_t)devmap_table);
    uart_puts("\n");
    
    if (devmap_table == NULL) {
        uart_puts("DEVMAP: ERROR - PHYS_TO_DMAP returned NULL!\n");
        uart_puts("DEVMAP: Physical address outside DMAP range\n");
        return;
    }
    
    /* Clear device mapping table */
    memset(devmap_table, 0, table_size);
    devmap_count = 0;
    devmap_next_va = DEVMAP_VA_START;
    
    uart_puts("DEVMAP: Allocated ");
    uart_puthex(pages_needed);
    uart_puts(" pages (");
    uart_puthex(table_size);
    uart_puts(" bytes) for device table\n");

    /* Detect current platform (still useful for platform-specific behavior) */
    for (int i = 0; platforms[i] != NULL; i++) {
        if (platforms[i]->detect && platforms[i]->detect()) {
            current_platform = platforms[i];
            break;
        }
    }

    /* No default platform - must be explicitly detected */
    if (!current_platform) {
        uart_puts("DEVMAP: WARNING: No platform detected\n");
    } else {
        uart_puts("DEVMAP: Detected platform: ");
        uart_puts(current_platform->name);
        uart_puts("\n");
    }

    /* Map all devices discovered from the device tree */
    /* This is the ONLY place we map devices - no duplication */
    int mapped = devmap_map_all_devices();
    if (mapped < 0) {
        uart_puts("DEVMAP: Failed to map devices from device tree\n");
    } else {
        uart_puts("DEVMAP: Mapped ");
        uart_puthex(mapped);
        uart_puts(" device resources\n");
    }

    devmap_initialized = true;
}

/* Allocate virtual address for device mapping */
static uint64_t devmap_alloc_va(size_t size)
{
    /* Align size to page boundary */
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* Check if we have space */
    if (devmap_next_va + size > DEVMAP_VA_END) {
        return 0;
    }

    uint64_t va = devmap_next_va;
    devmap_next_va += size;
    
    return va;
}

/* Add a device mapping entry */
int devmap_add_entry(const devmap_entry_t *entry)
{
    if (!entry || entry->size == 0) {
        return -1;
    }

    if (devmap_count >= DEVMAP_MAX_ENTRIES) {
        uart_puts("DEVMAP: Table full\n");
        return -1;
    }

    // uart_puts("  devmap_add_entry: copying entry\n");
    /* Copy entry */
    devmap_table[devmap_count].entry = *entry;
    devmap_table[devmap_count].in_use = true;

    // uart_puts("  devmap_add_entry: allocating VA\n");
    /* Allocate virtual address if not specified */
    if (entry->virt_addr == 0) {
        devmap_table[devmap_count].allocated_va = devmap_alloc_va(entry->size);
        if (devmap_table[devmap_count].allocated_va == 0) {
            uart_puts("DEVMAP: Failed to allocate VA for ");
            uart_puts(entry->name);
            uart_puts("\n");
            return -1;
        }
    } else {
        devmap_table[devmap_count].allocated_va = entry->virt_addr;
    }

    /* Map the device memory */
    uint64_t va = devmap_table[devmap_count].allocated_va;
    uint64_t pa = entry->phys_addr;
    size_t size = entry->size;
    
    /* Align PA down to page boundary and adjust size */
    uint64_t pa_offset = pa & (PAGE_SIZE - 1);
    uint64_t aligned_pa = pa & ~(PAGE_SIZE - 1);
    size_t aligned_size = (size + pa_offset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    /* Convert attributes to VMM flags */
    uint64_t vmm_attrs = VMM_ATTR_READ | VMM_ATTR_WRITE;
    if (entry->attributes & DEVMAP_ATTR_DEVICE) {
        vmm_attrs |= VMM_ATTR_DEVICE;
    } else if (entry->attributes & DEVMAP_ATTR_NOCACHE) {
        vmm_attrs |= VMM_ATTR_NOCACHE;
    }

    /* Debug output before mapping */
    // uart_puts("  devmap_add_entry: about to call vmm_map_range\n");
    // uart_puts("    PA=");
    // uart_puthex(pa);
    // uart_puts(" VA=");
    // uart_puthex(va);
    // uart_puts(" size=");
    // uart_puthex(size);
    // uart_puts("\n");
    
    /* Perform the mapping */
    if (!vmm_map_range(vmm_get_kernel_context(), va, aligned_pa, aligned_size, vmm_attrs)) {
        // uart_puts("DEVMAP: Failed to map ");
        // uart_puts(entry->name);
        // uart_puts(" at PA ");
        // uart_puthex(pa);
        // uart_puts(" to VA ");
        // uart_puthex(va);
        // uart_puts("\n");
        return -1;
    }

    // uart_puts("DEVMAP: Mapped ");
    // uart_puts(entry->name);
    // uart_puts(" at PA ");
    // uart_puthex(pa);
    // uart_puts(" to VA ");
    // uart_puthex(va);
    // uart_puts(" (");
    // uart_puthex(size);
    // uart_puts(" bytes)\n");

    devmap_count++;
    return 0;
}

/* Map a device at runtime */
void* devmap_map_device(uint64_t phys_addr, size_t size, uint32_t attributes)
{
    devmap_entry_t entry = {
        .name = "runtime",
        .phys_addr = phys_addr,
        .virt_addr = 0,  /* Allocate */
        .size = size,
        .attributes = attributes
    };

    if (devmap_add_entry(&entry) != 0) {
        return NULL;
    }

    /* Return the allocated virtual address with proper offset */
    uint64_t pa_offset = phys_addr & (PAGE_SIZE - 1);
    return (void*)(devmap_table[devmap_count - 1].allocated_va + pa_offset);
}

/* Get virtual address for a physical device address */
void* devmap_device_va(uint64_t phys_addr)
{
    for (int i = 0; i < devmap_count; i++) {
        if (!devmap_table[i].in_use) {
            continue;
        }

        uint64_t start = devmap_table[i].entry.phys_addr;
        uint64_t end = start + devmap_table[i].entry.size;

        if (phys_addr >= start && phys_addr < end) {
            /* Account for page alignment adjustments */
            uint64_t pa_offset = start & (PAGE_SIZE - 1);
            uint64_t offset = phys_addr - start;
            return (void*)(devmap_table[i].allocated_va + pa_offset + offset);
        }
    }

    return NULL;
}

/* Print all device mappings */
void devmap_print_mappings(void)
{
    uart_puts("\nDevice Memory Mappings:\n");
    uart_puts("Name                PA              VA              Size\n");
    uart_puts("--------------------------------------------------------\n");

    for (int i = 0; i < devmap_count; i++) {
        if (!devmap_table[i].in_use) {
            continue;
        }

        const devmap_entry_t *entry = &devmap_table[i].entry;
        
        /* Print name with padding */
        uart_puts(entry->name);
        int name_len = strlen(entry->name);
        for (int j = name_len; j < 20; j++) {
            uart_putc(' ');
        }

        /* Print physical address */
        uart_puthex(entry->phys_addr);
        uart_puts("  ");

        /* Print virtual address */
        uart_puthex(devmap_table[i].allocated_va);
        uart_puts("  ");

        /* Print size */
        uart_puthex(entry->size);
        uart_puts("\n");
    }
}

/* Device-aware mapping functions */

/* Find and map a device by compatible string */
void *devmap_find_and_map(const char *compatible) {
    struct device *dev;
    struct resource *res;
    
    /* Find device by compatible string */
    dev = device_find_by_compatible(compatible);
    if (!dev) {
        return NULL;
    }
    
    /* Get first memory resource */
    res = device_get_resource(dev, RES_TYPE_MEM, 0);
    if (!res) {
        return NULL;
    }
    
    /* Check if already mapped */
    if (res->mapped_addr) {
        return res->mapped_addr;
    }
    
    /* Map the device */
    uint64_t phys_addr = res->start;
    size_t size = resource_size(res);
    void *vaddr = devmap_map_device(phys_addr, size, DEVMAP_ATTR_DEVICE);
    
    if (vaddr) {
        /* Store mapped address in resource */
        resource_set_mapped_addr(res, vaddr);
    }
    
    return vaddr;
}

/* Map all resources for a device */
int devmap_map_device_resources(struct device *dev) {
    struct resource *res;
    int mapped = 0;
    
    if (!dev) {
        return -1;
    }
    
    /* Map all memory resources */
    for (int i = 0; i < DEVICE_MAX_RESOURCES; i++) {
        res = device_get_resource(dev, RES_TYPE_MEM, i);
        if (!res) {
            break;
        }
        
        /* Skip if already mapped */
        if (res->mapped_addr) {
            mapped++;
            continue;
        }
        
        /* Check if this physical address is already mapped */
        void *existing_va = devmap_device_va(res->start);
        if (existing_va) {
            /* Already mapped, just update the resource */
            resource_set_mapped_addr(res, existing_va);
            mapped++;
            continue;
        }
        
        /* Map the resource with device name */
        devmap_entry_t entry = {
            .name = dev->name,  /* Use device name instead of "runtime" */
            .phys_addr = res->start,
            .virt_addr = 0,  /* Allocate */
            .size = resource_size(res),
            .attributes = DEVMAP_ATTR_DEVICE
        };
        
        if (devmap_add_entry(&entry) == 0) {
            /* Get the allocated virtual address with proper offset */
            uint64_t pa_offset = res->start & (PAGE_SIZE - 1);
            void *vaddr = (void*)(devmap_table[devmap_count - 1].allocated_va + pa_offset);
            resource_set_mapped_addr(res, vaddr);
            mapped++;
        }
    }
    
    return mapped;
}

/* Platform registration (typically not used at runtime) */
void platform_register(const platform_desc_t *platform)
{
    /* This would be used for dynamic platform registration */
    /* Not implemented for static platform list */
}

/* Get current platform */
const platform_desc_t* platform_get_current(void)
{
    return current_platform;
}

/* Map all discovered devices */
int devmap_map_all_devices(void)
{
    struct device *dev;
    int mapped_count = 0;
    
    uart_puts("\nDEVMAP: Mapping all discovered devices\n");
    
    /* Iterate through all devices */
    dev = device_get_root();
    if (!dev) {
        uart_puts("DEVMAP: No device tree available\n");
        return -1;
    }
    
    /* Map devices recursively */
    mapped_count = devmap_map_device_tree(dev);
    
    uart_puts("DEVMAP: Mapped ");
    uart_puthex(mapped_count);
    uart_puts(" device resources\n");
    
    return mapped_count;
}

/* Recursively map devices in the device tree */
static int devmap_map_device_tree(struct device *dev)
{
    struct device *child;
    int count = 0;
    
    if (!dev) {
        return 0;
    }
    
    /* Skip the root device itself */
    if (dev != device_get_root()) {
        /* Map this device's resources */
        int mapped = devmap_map_device_resources(dev);
        if (mapped > 0) {
            count += mapped;
            // uart_puts("DEVMAP: Mapped ");
            // uart_puts(dev->name);
            // uart_puts(" (");
            // uart_puthex(mapped);
            // uart_puts(" resources)\n");
        }
    }
    
    /* Map children */
    for (child = dev->children; child; child = child->sibling) {
        count += devmap_map_device_tree(child);
    }
    
    return count;
}
