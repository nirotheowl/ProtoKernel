/*
 * kernel/memory/devmap.c
 *
 * Device memory mapping infrastructure
 * Manages platform-specific device mappings in kernel virtual address space
 */

#include <platform/devmap.h>
#include <memory/vmm.h>
#include <memory/vmparam.h>
#include <uart.h>
#include <string.h>
#include <stddef.h>

/* Device mapping configuration */
#define DEVMAP_MAX_ENTRIES      64
#define DEVMAP_VA_START         0xFFFF000100000000UL  /* Start after kernel space */
#define DEVMAP_VA_END           0xFFFF000200000000UL  /* 1TB for device mappings */

/* Device mapping table */
static struct {
    devmap_entry_t entry;
    uint64_t allocated_va;
    bool in_use;
} devmap_table[DEVMAP_MAX_ENTRIES];

static int devmap_count = 0;
static uint64_t devmap_next_va = DEVMAP_VA_START;
static bool devmap_initialized = false;

/* Current platform */
static const platform_desc_t *current_platform = NULL;

/* Forward declarations */
extern const platform_desc_t qemu_virt_platform;
extern const platform_desc_t odroid_m2_platform;

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

    /* Clear device mapping table */
    memset(devmap_table, 0, sizeof(devmap_table));
    devmap_count = 0;
    devmap_next_va = DEVMAP_VA_START;

    /* Detect current platform */
    for (int i = 0; platforms[i] != NULL; i++) {
        if (platforms[i]->detect && platforms[i]->detect()) {
            current_platform = platforms[i];
            // uart_puts("DEVMAP: Detected platform: ");
            // uart_puts(current_platform->name);
            // uart_puts("\n");
            break;
        }
    }

    /* Default to QEMU if no platform detected */
    if (!current_platform) {
        current_platform = &qemu_virt_platform;
        // uart_puts("DEVMAP: No platform detected, defaulting to QEMU virt\n");
    }

    /* Map all devices for current platform */
    if (current_platform->devmap && current_platform->devmap->entries) {
        const devmap_entry_t *entry = current_platform->devmap->entries;
        while (entry->size != 0) {
            // uart_puts("DEVMAP: Adding ");
            // uart_puts(entry->name);
            // uart_puts(" at PA ");
            // uart_puthex(entry->phys_addr);
            // uart_puts("\n");
            
            if (devmap_add_entry(entry) != 0) {
                uart_puts("DEVMAP: Failed to add entry: ");
                uart_puts(entry->name);
                uart_puts("\n");
            }
            entry++;
        }
    }

    devmap_initialized = true;
    // uart_puts("DEVMAP: Device mapping initialized\n");
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
    if (!vmm_map_range(vmm_get_kernel_context(), va, pa, size, vmm_attrs)) {
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

    /* Return the allocated virtual address */
    return (void*)devmap_table[devmap_count - 1].allocated_va;
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
            uint64_t offset = phys_addr - start;
            return (void*)(devmap_table[i].allocated_va + offset);
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