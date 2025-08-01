/*
 * kernel/include/memory/devmap.h
 *
 * Device memory mapping infrastructure
 * Manages virtual address mappings for device MMIO regions
 */

#ifndef __MEMORY_DEVMAP_H__
#define __MEMORY_DEVMAP_H__

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct device;

/* Device memory attributes */
#define DEVMAP_ATTR_DEVICE      0x01    /* Device memory (nGnRnE) */
#define DEVMAP_ATTR_NOCACHE     0x02    /* Normal memory, non-cacheable */
#define DEVMAP_ATTR_WRITETHROUGH 0x04   /* Write-through cacheable */
#define DEVMAP_ATTR_WRITEBACK   0x08    /* Write-back cacheable */

/* Device mapping entry */
typedef struct devmap_entry {
    const char *name;           /* Device name for debugging */
    uint64_t phys_addr;        /* Physical address */
    uint64_t virt_addr;        /* Virtual address (0 = allocate) */
    size_t size;               /* Size in bytes */
    uint32_t attributes;       /* Memory attributes */
} devmap_entry_t;

/* Platform device map table - terminated by entry with size == 0 */
typedef struct platform_devmap {
    const char *platform_name;
    const devmap_entry_t *entries;
} platform_devmap_t;

/* Platform detection function type */
typedef int (*platform_detect_fn)(void);

/* Platform descriptor */
typedef struct platform_desc {
    const char *name;
    platform_detect_fn detect;
    const platform_devmap_t *devmap;
    uint64_t console_uart_phys;     /* Physical address of console UART */
    const char *console_uart_compatible; /* Compatible string for console UART */
} platform_desc_t;

/* Device mapping functions */
void devmap_init(void);
int devmap_add_entry(const devmap_entry_t *entry);
void* devmap_map_device(uint64_t phys_addr, size_t size, uint32_t attributes);
void* devmap_device_va(uint64_t phys_addr);
void devmap_print_mappings(void);

/* Device-aware mapping functions */
void *devmap_find_and_map(const char *compatible);
int devmap_map_device_resources(struct device *dev);
int devmap_map_all_devices(void);

/* Platform registration */
void platform_register(const platform_desc_t *platform);
const platform_desc_t* platform_get_current(void);

#endif /* __MEMORY_DEVMAP_H__ */