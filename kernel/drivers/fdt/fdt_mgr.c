/*
 * kernel/drivers/fdt/fdt_mgr.c
 * 
 * FDT (Flattened Device Tree) Manager
 * Handles preservation and runtime access to the device tree blob
 */

#include <drivers/fdt_mgr.h>
#include <drivers/fdt.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <memory/paging.h>
#include <memory/vmparam.h>
#include <uart.h>
#include <string.h>

/* External symbols from linker script */
extern char _kernel_end;

/* Global FDT manager state */
static fdt_mgr_state_t fdt_state = {
    .phys_addr = NULL,
    .virt_addr = NULL,
    .size = 0,
    .is_mapped = false,
    .is_relocated = false
};

/* Initialize FDT manager with DTB from boot */
bool fdt_mgr_init(void *dtb_phys) {
    fdt_header_t *header;
    uint32_t totalsize;
    
    if (!dtb_phys) {
        // uart_puts("FDT_MGR: No DTB provided\n");
        return false;
    }
    
    /* Save physical address */
    fdt_state.phys_addr = dtb_phys;
    
    // uart_puts("FDT_MGR: DTB physical address: ");
    // uart_puthex((uint64_t)dtb_phys);
    // uart_puts("\n");
    
    /* Map temporarily to read header (using identity mapping) */
    header = (fdt_header_t *)dtb_phys;
    
    // uart_puts("FDT_MGR: Reading magic at ");
    // uart_puthex((uint64_t)&header->magic);
    // uart_puts(": ");
    // uart_puthex(header->magic);
    // uart_puts(" (expected ");
    // uart_puthex(FDT_MAGIC);
    // uart_puts(")\n");
    
    /* Validate magic number */
    if (fdt32_to_cpu(header->magic) != FDT_MAGIC) {
        // uart_puts("FDT_MGR: Invalid FDT magic after byte swap: ");
        // uart_puthex(fdt32_to_cpu(header->magic));
        // uart_puts("\n");
        return false;
    }
    
    /* Get total size */
    totalsize = fdt32_to_cpu(header->totalsize);
    
    if (totalsize > FDT_MAX_SIZE) {
        // uart_puts("FDT_MGR: WARNING: FDT size (");
        // uart_puthex(totalsize);
        // uart_puts(" bytes) exceeds max (");
        // uart_puthex(FDT_MAX_SIZE);
        // uart_puts(" bytes)\n");
        // For now, continue anyway as QEMU generates large DTBs
    }
    
    fdt_state.size = totalsize;
    
    /* Check if DTB was relocated by boot.S */
    uint64_t kernel_end = (uint64_t)&_kernel_end;
    uint64_t dtb_addr = (uint64_t)dtb_phys;
    if (dtb_addr >= kernel_end && dtb_addr < (kernel_end + 0x20000)) {
        fdt_state.is_relocated = true;
        // uart_puts("FDT_MGR: Using relocated DTB at ");
        // uart_puthex(dtb_addr);
        // uart_puts("\n");
    }
    
    // uart_puts("FDT_MGR: Initialized with DTB at ");
    // uart_puthex((uint64_t)dtb_phys);
    // uart_puts(", size ");
    // uart_puthex(totalsize);
    // uart_puts(" bytes\n");
    
    return true;
}

/* Reserve FDT pages in PMM */
bool fdt_mgr_reserve_pages(void) {
    if (!fdt_state.phys_addr || !fdt_state.size) {
        return false;
    }
    
    /* Calculate page-aligned range */
    uint64_t start_page = (uint64_t)fdt_state.phys_addr & ~(PAGE_SIZE - 1);
    uint64_t end_page = ((uint64_t)fdt_state.phys_addr + fdt_state.size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t num_pages = (end_page - start_page) / PAGE_SIZE;
    
    // uart_puts("FDT_MGR: Reserving ");
    // uart_puthex(num_pages);
    // uart_puts(" pages at ");
    // uart_puthex(start_page);
    // uart_puts("\n");
    
    /* Mark pages as reserved in PMM */
    for (uint64_t i = 0; i < num_pages; i++) {
        pmm_reserve_page(start_page + (i * PAGE_SIZE));
    }
    
    return true;
}

/* Map FDT to permanent virtual address */
bool fdt_mgr_map_virtual(void) {
    if (!fdt_state.phys_addr || !fdt_state.size) {
        return false;
    }
    
    if (fdt_state.is_mapped) {
        return true;  /* Already mapped */
    }
    
    /* Calculate page-aligned range */
    uint64_t phys_start = (uint64_t)fdt_state.phys_addr & ~(PAGE_SIZE - 1);
    uint64_t offset = (uint64_t)fdt_state.phys_addr - phys_start;
    uint64_t map_size = ((fdt_state.size + offset + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    // uart_puts("FDT_MGR: Mapping FDT to virtual address ");
    // uart_puthex(FDT_VIRT_BASE);
    // uart_puts("\n");
    
    /* Map the FDT pages with read-only permissions */
    uint32_t attrs = VMM_ATTR_READ;
    if (!vmm_map_range(vmm_get_kernel_context(), FDT_VIRT_BASE, phys_start, map_size, attrs)) {
        // uart_puts("FDT_MGR: Failed to map FDT to virtual memory\n");
        return false;
    }
    
    /* Calculate virtual address including offset */
    fdt_state.virt_addr = (void *)(FDT_VIRT_BASE + offset);
    fdt_state.is_mapped = true;
    
    /* Validate the mapping by checking magic number */
    fdt_header_t *header = (fdt_header_t *)fdt_state.virt_addr;
    if (fdt32_to_cpu(header->magic) != FDT_MAGIC) {
        // uart_puts("FDT_MGR: FDT validation failed after mapping\n");
        fdt_state.is_mapped = false;
        return false;
    }
    
    // uart_puts("FDT_MGR: Successfully mapped FDT\n");
    return true;
}

/* Get virtual address of FDT (after mapping) */
void *fdt_mgr_get_blob(void) {
    if (fdt_state.is_mapped) {
        return fdt_state.virt_addr;
    }
    /* If not mapped yet, return physical address (assumes identity mapping) */
    return fdt_state.phys_addr;
}

/* Get physical address of FDT */
void *fdt_mgr_get_phys_addr(void) {
    return fdt_state.phys_addr;
}

/* Get size of FDT blob */
size_t fdt_mgr_get_size(void) {
    return fdt_state.size;
}

/* Check if FDT is accessible */
bool fdt_mgr_is_available(void) {
    return fdt_state.phys_addr != NULL && fdt_state.size > 0;
}

/* Calculate simple checksum of FDT for integrity checking */
static uint32_t fdt_calculate_checksum(void *fdt, size_t size) {
    uint32_t checksum = 0;
    uint8_t *bytes = (uint8_t *)fdt;
    
    for (size_t i = 0; i < size; i++) {
        checksum = ((checksum << 1) | (checksum >> 31)) ^ bytes[i];
    }
    
    return checksum;
}

/* Verify FDT integrity */
bool fdt_mgr_verify_integrity(void) {
    if (!fdt_state.phys_addr || !fdt_state.size) {
        return false;
    }
    
    void *fdt = fdt_mgr_get_blob();
    if (!fdt) {
        return false;
    }
    
    /* Check magic number */
    fdt_header_t *header = (fdt_header_t *)fdt;
    if (fdt32_to_cpu(header->magic) != FDT_MAGIC) {
        // uart_puts("FDT_MGR: Integrity check failed - invalid magic\n");
        return false;
    }
    
    /* Verify size matches */
    uint32_t totalsize = fdt32_to_cpu(header->totalsize);
    if (totalsize != fdt_state.size) {
        // uart_puts("FDT_MGR: Integrity check failed - size mismatch\n");
        return false;
    }
    
    /* Verify structure offsets are within bounds */
    uint32_t struct_off = fdt32_to_cpu(header->off_dt_struct);
    uint32_t strings_off = fdt32_to_cpu(header->off_dt_strings);
    uint32_t struct_size = fdt32_to_cpu(header->size_dt_struct);
    uint32_t strings_size = fdt32_to_cpu(header->size_dt_strings);
    
    if (struct_off + struct_size > totalsize || 
        strings_off + strings_size > totalsize) {
        // uart_puts("FDT_MGR: Integrity check failed - invalid offsets\n");
        return false;
    }
    
    return true;
}

/* Debug: Print FDT manager state */
void fdt_mgr_print_info(void) {
    uart_puts("\nFDT Manager State:\n");
    uart_puts("==================\n");
    uart_puts("Physical address: ");
    // uart_puthex((uint64_t)fdt_state.phys_addr);
    uart_puts("\nVirtual address:  ");
    if (fdt_state.is_mapped) {
        // uart_puthex((uint64_t)fdt_state.virt_addr);
    } else {
        uart_puts("(not mapped)");
    }
    uart_puts("\nSize:             ");
    // uart_puthex(fdt_state.size);
    uart_puts(" bytes\n");
    uart_puts("Is mapped:        ");
    uart_puts(fdt_state.is_mapped ? "yes" : "no");
    uart_puts("\nIs relocated:     ");
    uart_puts(fdt_state.is_relocated ? "yes" : "no");
    uart_puts("\n");
    
    /* Add integrity check */
    if (fdt_state.is_mapped) {
        uart_puts("Integrity check:  ");
        if (fdt_mgr_verify_integrity()) {
            uart_puts("PASS");
        } else {
            uart_puts("FAIL");
        }
        uart_puts("\n");
    }
}

/* Get memory regions (convenience wrapper) */
bool fdt_mgr_get_memory_info(memory_info_t *mem_info) {
    void *fdt = fdt_mgr_get_blob();
    if (!fdt) {
        return false;
    }
    return fdt_get_memory(fdt, mem_info);
}

/* Helper to walk FDT nodes */
static int fdt_walk_nodes(void *fdt, int offset, int depth, 
                         fdt_device_callback_t callback, void *ctx) {
    int child;
    const char *name;
    char path[256];
    int ret;
    
    /* Get node name */
    name = fdt_get_name(fdt, offset, NULL);
    if (!name) {
        return -1;
    }
    
    /* Build path (simplified - doesn't handle full paths) */
    if (depth == 0) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        /* For now, just use the name */
        strncpy(path, name, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    
    /* Call callback if provided */
    if (callback) {
        ret = callback(path, name, ctx);
        if (ret != 0) {
            return ret;
        }
    }
    
    /* Recurse through children */
    fdt_for_each_subnode(child, fdt, offset) {
        ret = fdt_walk_nodes(fdt, child, depth + 1, callback, ctx);
        if (ret != 0) {
            return ret;
        }
    }
    
    return 0;
}

/* Enumerate all devices in FDT */
int fdt_mgr_enumerate_devices(fdt_device_callback_t callback, void *ctx) {
    void *fdt = fdt_mgr_get_blob();
    if (!fdt) {
        return -1;
    }
    
    /* Start from root node */
    return fdt_walk_nodes(fdt, 0, 0, callback, ctx);
}

/* Find a device by compatible string */
int fdt_mgr_find_compatible(const char *compatible, char *path, size_t pathlen) {
    /* TODO: Implement compatible string search */
    (void)compatible;
    (void)path;
    (void)pathlen;
    return -1;
}

/* Get property from a device node */
bool fdt_mgr_get_property(const char *path, const char *prop, void *buf, size_t *len) {
    /* TODO: Implement property retrieval */
    (void)path;
    (void)prop;
    (void)buf;
    (void)len;
    return false;
}