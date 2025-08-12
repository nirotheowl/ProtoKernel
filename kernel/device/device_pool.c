/*
 * kernel/device/device_pool.c
 * 
 * Device memory pool management
 * Provides memory allocation for device structures using PMM-based allocation
 */

#include <device/device.h>
#include <device/resource.h>
#include <device/device_tree.h>
#include <memory/vmm.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <string.h>
#include <uart.h>

/* External symbols from linker script */
extern char _kernel_end;

/* Pool configuration */
#define POOL_ALIGN              16              /* 16-byte alignment for allocations */
#define PMM_POOL_MAGIC          0x504D4D01      /* "PMM" pool */

/* Pool header for tracking */
typedef struct {
    uint32_t    magic;          /* Magic number for validation */
    size_t      size;           /* Total pool size */
    size_t      used;           /* Current usage */
    size_t      peak_usage;     /* Peak usage for monitoring */
    uint32_t    allocations;    /* Number of allocations */
    void        *next;          /* Next free location */
} pool_header_t;

/* Pool initialization flag */
static bool pool_initialized = false;

/* PMM-based pool state */
static struct {
    void *base;             /* Virtual base address */
    uint64_t phys_addr;     /* Physical base address */
    size_t size;            /* Total size */
    size_t used;            /* Current usage */
    bool active;            /* Using PMM pool */
} pmm_pool = {0};

/* Statistics for debugging */
static struct {
    uint32_t device_allocs;
    uint32_t resource_allocs;
    uint32_t string_allocs;
    uint32_t misc_allocs;
    size_t   total_allocated;
} pool_stats = {0};

/* Initialize the device pool - must be called after PMM is initialized */
bool device_pool_init(void) {
    size_t pool_pages = 128;  /* 512KB pool to support device-rich platforms */
    uint64_t phys_addr;
    pool_header_t *header;
    
    if (pool_initialized) {
        return true;
    }
    
    /* We now require PMM to be initialized first */
    if (!pmm_is_initialized()) {
        uart_puts("DEVICE_POOL: ERROR - PMM must be initialized first\n");
        return false;
    }
    
    /* Allocate physical pages from PMM */
    phys_addr = pmm_alloc_pages(pool_pages);
    if (!phys_addr) {
        uart_puts("DEVICE_POOL: Failed to allocate PMM pages\n");
        return false;
    }
    
    pmm_pool.size = pool_pages * PAGE_SIZE;
    pmm_pool.phys_addr = phys_addr;
    
    /* Use DMAP to access the pool memory */
    /* Since we allocated from PMM, this memory is regular RAM and should be in DMAP */
    pmm_pool.base = (void *)PHYS_TO_DMAP(phys_addr);
    
    if (!pmm_pool.base) {
        uart_puts("DEVICE_POOL: Failed to get DMAP address for pool\n");
        pmm_free_pages(phys_addr, pool_pages);
        return false;
    }
    
    uart_puts("DEVICE_POOL: Allocated pool at PA ");
    uart_puthex(phys_addr);
    uart_puts(", accessible via DMAP at VA ");
    uart_puthex((uint64_t)pmm_pool.base);
    uart_puts("\n");
    
    /* Initialize pool header at the beginning of the allocated space */
    header = (pool_header_t *)pmm_pool.base;
    header->magic = PMM_POOL_MAGIC;
    header->size = pmm_pool.size;
    header->used = sizeof(pool_header_t);
    header->peak_usage = sizeof(pool_header_t);
    header->allocations = 0;
    header->next = (uint8_t *)header + sizeof(pool_header_t);
    
    pmm_pool.used = sizeof(pool_header_t);
    pmm_pool.active = true;
    pool_initialized = true;
    
    uart_puts("DEVICE_POOL: Initialized with PMM at ");
    uart_puthex((uint64_t)pmm_pool.base);
    uart_puts(" (size=");
    uart_puthex(pmm_pool.size);
    uart_puts(")\n");
    
    return true;
}

/* Align a size to the pool alignment */
static size_t align_size(size_t size) {
    return (size + POOL_ALIGN - 1) & ~(POOL_ALIGN - 1);
}


/* Allocate memory from the active pool */
void *device_pool_alloc(size_t size) {
    void *ptr;
    size_t aligned_size;
    
    if (!pool_initialized) {
        uart_puts("DEVICE_POOL: ERROR - Pool not initialized\n");
        return NULL;
    }
    
    if (size == 0) {
        return NULL;
    }
    
    /* Align the size */
    aligned_size = align_size(size);
    
    /* Check if we need more space */
    if (pmm_pool.used + aligned_size > pmm_pool.size) {
        /* Try to allocate more pages */
        size_t extra_pages = (aligned_size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t new_phys = pmm_alloc_pages(extra_pages);
        if (!new_phys) {
            uart_puts("DEVICE_POOL: Failed to expand PMM pool\n");
            return NULL;
        }
        
        /* Map additional pages */
        if (!vmm_map_range(vmm_get_kernel_context(),
                           (uint64_t)pmm_pool.base + pmm_pool.size,
                           new_phys,
                           extra_pages * PAGE_SIZE,
                           VMM_ATTR_READ | VMM_ATTR_WRITE)) {
            pmm_free_pages(new_phys, extra_pages);
            return NULL;
        }
        
        pmm_pool.size += extra_pages * PAGE_SIZE;
        
        /* Update header size */
        pool_header_t *header = (pool_header_t *)pmm_pool.base;
        header->size = pmm_pool.size;
    }
    
    /* Update pool header */
    pool_header_t *header = (pool_header_t *)pmm_pool.base;
    ptr = header->next;
    header->next = (uint8_t *)header->next + aligned_size;
    header->used += aligned_size;
    header->allocations++;
    
    if (header->used > header->peak_usage) {
        header->peak_usage = header->used;
    }
    
    pmm_pool.used += aligned_size;
    
    /* Update statistics */
    pool_stats.total_allocated += aligned_size;
    pool_stats.misc_allocs++;
    
    /* Zero the allocated memory */
    memset(ptr, 0, size);
    
    return ptr;
}

/* Allocate a device structure from early pool */
struct device *device_pool_alloc_device(void) {
    struct device *dev;
    
    dev = (struct device *)device_pool_alloc(sizeof(struct device));
    if (dev) {
        pool_stats.device_allocs++;
        pool_stats.misc_allocs--;  /* Correct the counter */
    }
    
    return dev;
}

/* Allocate a resource structure from early pool */
struct resource *device_pool_alloc_resource(void) {
    struct resource *res;
    
    res = (struct resource *)device_pool_alloc(sizeof(struct resource));
    if (res) {
        pool_stats.resource_allocs++;
        pool_stats.misc_allocs--;  /* Correct the counter */
    }
    
    return res;
}

/* Allocate string storage from early pool */
char *device_pool_strdup(const char *str) {
    size_t len;
    char *copy;
    
    if (!str) {
        return NULL;
    }
    
    len = strlen(str) + 1;
    copy = (char *)device_pool_alloc(len);
    if (copy) {
        memcpy(copy, str, len);
        pool_stats.string_allocs++;
        pool_stats.misc_allocs--;  /* Correct the counter */
    }
    
    return copy;
}

/* Allocate and copy a memory region */
void *device_pool_memdup(const void *src, size_t size) {
    void *copy;
    
    if (!src || size == 0) {
        return NULL;
    }
    
    copy = device_pool_alloc(size);
    if (copy) {
        memcpy(copy, src, size);
    }
    
    return copy;
}

/* Get current pool usage */
size_t device_pool_get_usage(void) {
    if (!pool_initialized) {
        return 0;
    }
    pool_header_t *header = (pool_header_t *)pmm_pool.base;
    return header->used;
}

/* Get remaining pool space */
size_t device_pool_get_free(void) {
    if (!pool_initialized) {
        return 0;
    }
    pool_header_t *header = (pool_header_t *)pmm_pool.base;
    return header->size - header->used;
}

/* Get pool statistics */
void device_pool_get_stats(uint32_t *devices, uint32_t *resources, 
                         uint32_t *strings, size_t *total) {
    if (devices) *devices = pool_stats.device_allocs;
    if (resources) *resources = pool_stats.resource_allocs;
    if (strings) *strings = pool_stats.string_allocs;
    if (total) *total = pool_stats.total_allocated;
}

/* Print pool statistics for debugging */
void device_pool_print_stats(void) {
    if (!pool_initialized) {
        uart_puts("DEVICE_POOL: Not initialized\n");
        return;
    }
    
    uart_puts("\nDevice Pool Statistics:\n");
    uart_puts("=======================\n");
    
    pool_header_t *header = (pool_header_t *)pmm_pool.base;
    
    uart_puts("Using PMM-based pool:\n");
    uart_puts("  Virtual base:  ");
    uart_puthex((uint64_t)pmm_pool.base);
    uart_puts("\n  Physical base: ");
    uart_puthex(pmm_pool.phys_addr);
    uart_puts("\n  Total size:    ");
    uart_puthex(header->size);
    uart_puts(" bytes\n  Used:          ");
    uart_puthex(header->used);
    uart_puts(" bytes (");
    uart_puthex((header->used * 100) / header->size);
    uart_puts("%)\n  Free:          ");
    uart_puthex(header->size - header->used);
    uart_puts(" bytes\n  Peak usage:    ");
    uart_puthex(header->peak_usage);
    uart_puts(" bytes\n");
    
    uart_puts("\nAllocation breakdown:\n");
    uart_puts("  Devices:       ");
    uart_puthex(pool_stats.device_allocs);
    uart_puts(" (");
    uart_puthex(pool_stats.device_allocs * sizeof(struct device));
    uart_puts(" bytes)\n");
    uart_puts("  Resources:     ");
    uart_puthex(pool_stats.resource_allocs);
    uart_puts(" (");
    uart_puthex(pool_stats.resource_allocs * sizeof(struct resource));
    uart_puts(" bytes)\n");
    uart_puts("  Strings:       ");
    uart_puthex(pool_stats.string_allocs);
    uart_puts("\n");
    uart_puts("  Other:         ");
    uart_puthex(pool_stats.misc_allocs);
    uart_puts("\n");
    uart_puts("  Total allocs:  ");
    uart_puthex(header->allocations);
    uart_puts("\n");
}

/* Check pool integrity */
bool device_pool_check_integrity(void) {
    pool_header_t *header;
    uint8_t *expected_next;
    
    if (!pool_initialized) {
        return false;
    }
    
    header = (pool_header_t *)pmm_pool.base;
    
    /* Check magic number */
    if (header->magic != PMM_POOL_MAGIC) {
        uart_puts("DEVICE_POOL: ERROR - Invalid magic number\n");
        return false;
    }
    
    /* Check bounds */
    if (header->used > header->size) {
        uart_puts("DEVICE_POOL: ERROR - Pool overflow detected\n");
        return false;
    }
    
    /* Check next pointer */
    expected_next = (uint8_t *)header + header->used;
    if (header->next != expected_next) {
        uart_puts("DEVICE_POOL: ERROR - Next pointer corruption\n");
        return false;
    }
    
    return true;
}

/* Reset pool (for testing only) */
void device_pool_reset(void) {
    pool_header_t *header;
    
    if (!pool_initialized) {
        return;
    }
    
    /* Clear statistics */
    memset(&pool_stats, 0, sizeof(pool_stats));
    
    /* Reset pool header */
    header = (pool_header_t *)pmm_pool.base;
    header->used = sizeof(pool_header_t);
    header->allocations = 0;
    header->next = (uint8_t *)header + sizeof(pool_header_t);
    
    /* Reset pmm_pool tracking */
    pmm_pool.used = sizeof(pool_header_t);
}