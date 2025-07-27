/*
 * kernel/include/drivers/fdt_mgr.h
 * 
 * FDT (Flattened Device Tree) Manager
 * Handles preservation and runtime access to the device tree blob
 */

#ifndef __FDT_MGR_H__
#define __FDT_MGR_H__

#include <stdint.h>
#include <stdbool.h>
#include <drivers/fdt.h>

/* FDT virtual address allocation 
 * Place FDT well after device mappings but before DMAP
 * Using 0xFFFF0002xxxxxxxx to ensure no conflicts
 */
#define FDT_VIRT_BASE    0xFFFF000200000000UL  /* Virtual address for FDT mapping */
#define FDT_MAX_SIZE     (2 * 1024 * 1024)     /* Maximum FDT size: 2MB */

/* FDT manager state */
typedef struct {
    void *phys_addr;       /* Physical address of FDT */
    void *virt_addr;       /* Virtual address after mapping */
    size_t size;           /* Size of FDT blob */
    bool is_mapped;        /* Whether FDT is mapped to virtual memory */
    bool is_relocated;     /* Whether FDT was relocated by boot.S */
} fdt_mgr_state_t;

/* Initialize FDT manager with DTB from boot */
bool fdt_mgr_init(void *dtb_phys);

/* Get virtual address of FDT (after mapping) */
void *fdt_mgr_get_blob(void);

/* Get physical address of FDT */
void *fdt_mgr_get_phys_addr(void);

/* Get size of FDT blob */
size_t fdt_mgr_get_size(void);

/* Map FDT to permanent virtual address */
bool fdt_mgr_map_virtual(void);

/* Check if FDT is accessible */
bool fdt_mgr_is_available(void);

/* Reserve FDT pages in PMM */
bool fdt_mgr_reserve_pages(void);

/* Verify FDT integrity */
bool fdt_mgr_verify_integrity(void);

/* Debug: Print FDT manager state */
void fdt_mgr_print_info(void);

/* Device enumeration helpers */
typedef int (*fdt_device_callback_t)(const char *path, const char *name, void *ctx);

/* Enumerate all devices in FDT */
int fdt_mgr_enumerate_devices(fdt_device_callback_t callback, void *ctx);

/* Find a device by compatible string */
int fdt_mgr_find_compatible(const char *compatible, char *path, size_t pathlen);

/* Get property from a device node */
bool fdt_mgr_get_property(const char *path, const char *prop, void *buf, size_t *len);

/* Get memory regions (convenience wrapper) */
bool fdt_mgr_get_memory_info(memory_info_t *mem_info);

#endif /* __FDT_MGR_H__ */