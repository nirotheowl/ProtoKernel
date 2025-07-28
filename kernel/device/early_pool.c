/*
 * kernel/device/early_pool.c
 * 
 * Early boot memory pool for device allocation
 * Provides memory allocation before PMM is initialized
 */

#include <device/device.h>
#include <device/resource.h>
#include <device/device_tree.h>
#include <memory/paging.h>
#include <string.h>
#include <uart.h>

/* External symbols from linker script */
extern char _kernel_end;

/* Early pool configuration */
#define EARLY_POOL_SIZE         (512 * 1024)    /* 512KB */
#define EARLY_POOL_ALIGN        16              /* 16-byte alignment for allocations */
#define EARLY_POOL_MAGIC        0xEA51B001      /* "EARLY BOOT" */

/* Early pool header for tracking */
typedef struct {
    uint32_t    magic;          /* Magic number for validation */
    size_t      size;           /* Total pool size */
    size_t      used;           /* Current usage */
    size_t      peak_usage;     /* Peak usage for monitoring */
    uint32_t    allocations;    /* Number of allocations */
    void        *next;          /* Next free location */
} early_pool_header_t;

/* Static early pool allocation */
static uint8_t early_pool_data[EARLY_POOL_SIZE] __attribute__((aligned(PAGE_SIZE)));
static early_pool_header_t *early_pool = NULL;
static bool early_pool_initialized = false;

/* Statistics for debugging */
static struct {
    uint32_t device_allocs;
    uint32_t resource_allocs;
    uint32_t string_allocs;
    uint32_t misc_allocs;
    size_t   total_allocated;
} early_stats = {0};

/* Initialize the early boot memory pool */
bool early_pool_init(void) {
    if (early_pool_initialized) {
        return true;
    }
    
    /* Use the statically allocated pool */
    early_pool = (early_pool_header_t *)early_pool_data;
    
    /* Initialize header */
    early_pool->magic = EARLY_POOL_MAGIC;
    early_pool->size = EARLY_POOL_SIZE;
    early_pool->used = sizeof(early_pool_header_t);
    early_pool->peak_usage = sizeof(early_pool_header_t);
    early_pool->allocations = 0;
    early_pool->next = (uint8_t *)early_pool + sizeof(early_pool_header_t);
    
    early_pool_initialized = true;
    
    // uart_puts("EARLY_POOL: Initialized at ");
    // uart_puthex((uint64_t)early_pool);
    // uart_puts(" with ");
    // uart_puthex(EARLY_POOL_SIZE);
    // uart_puts(" bytes\n");
    
    return true;
}

/* Align a size to the pool alignment */
static size_t align_size(size_t size) {
    return (size + EARLY_POOL_ALIGN - 1) & ~(EARLY_POOL_ALIGN - 1);
}

/* Allocate memory from the early pool */
void *early_pool_alloc(size_t size) {
    void *ptr;
    size_t aligned_size;
    
    if (!early_pool_initialized) {
        uart_puts("EARLY_POOL: ERROR - Pool not initialized\n");
        return NULL;
    }
    
    if (size == 0) {
        return NULL;
    }
    
    /* Align the size */
    aligned_size = align_size(size);
    
    /* Check if we have enough space */
    if (early_pool->used + aligned_size > early_pool->size) {
        uart_puts("EARLY_POOL: ERROR - Out of memory (requested ");
        uart_puthex(size);
        uart_puts(", used ");
        uart_puthex(early_pool->used);
        uart_puts("/");
        uart_puthex(early_pool->size);
        uart_puts(")\n");
        return NULL;
    }
    
    /* Allocate from the pool */
    ptr = early_pool->next;
    early_pool->next = (uint8_t *)early_pool->next + aligned_size;
    early_pool->used += aligned_size;
    early_pool->allocations++;
    
    /* Update peak usage */
    if (early_pool->used > early_pool->peak_usage) {
        early_pool->peak_usage = early_pool->used;
    }
    
    /* Update statistics */
    early_stats.total_allocated += aligned_size;
    early_stats.misc_allocs++;
    
    /* Zero the allocated memory */
    memset(ptr, 0, size);
    
    return ptr;
}

/* Allocate a device structure from early pool */
struct device *early_pool_alloc_device(void) {
    struct device *dev;
    
    dev = (struct device *)early_pool_alloc(sizeof(struct device));
    if (dev) {
        early_stats.device_allocs++;
        early_stats.misc_allocs--;  /* Correct the counter */
    }
    
    return dev;
}

/* Allocate a resource structure from early pool */
struct resource *early_pool_alloc_resource(void) {
    struct resource *res;
    
    res = (struct resource *)early_pool_alloc(sizeof(struct resource));
    if (res) {
        early_stats.resource_allocs++;
        early_stats.misc_allocs--;  /* Correct the counter */
    }
    
    return res;
}

/* Allocate string storage from early pool */
char *early_pool_strdup(const char *str) {
    size_t len;
    char *copy;
    
    if (!str) {
        return NULL;
    }
    
    len = strlen(str) + 1;
    copy = (char *)early_pool_alloc(len);
    if (copy) {
        memcpy(copy, str, len);
        early_stats.string_allocs++;
        early_stats.misc_allocs--;  /* Correct the counter */
    }
    
    return copy;
}

/* Allocate and copy a memory region */
void *early_pool_memdup(const void *src, size_t size) {
    void *copy;
    
    if (!src || size == 0) {
        return NULL;
    }
    
    copy = early_pool_alloc(size);
    if (copy) {
        memcpy(copy, src, size);
    }
    
    return copy;
}

/* Get current pool usage */
size_t early_pool_get_usage(void) {
    if (!early_pool_initialized) {
        return 0;
    }
    return early_pool->used;
}

/* Get remaining pool space */
size_t early_pool_get_free(void) {
    if (!early_pool_initialized) {
        return 0;
    }
    return early_pool->size - early_pool->used;
}

/* Get pool statistics */
void early_pool_get_stats(uint32_t *devices, uint32_t *resources, 
                         uint32_t *strings, size_t *total) {
    if (devices) *devices = early_stats.device_allocs;
    if (resources) *resources = early_stats.resource_allocs;
    if (strings) *strings = early_stats.string_allocs;
    if (total) *total = early_stats.total_allocated;
}

/* Print pool statistics for debugging */
void early_pool_print_stats(void) {
    if (!early_pool_initialized) {
        uart_puts("EARLY_POOL: Not initialized\n");
        return;
    }
    
    uart_puts("\nEarly Boot Pool Statistics:\n");
    uart_puts("===========================\n");
    uart_puts("Pool address:    ");
    uart_puthex((uint64_t)early_pool);
    uart_puts("\nPool size:       ");
    uart_puthex(early_pool->size);
    uart_puts(" bytes\n");
    uart_puts("Used:            ");
    uart_puthex(early_pool->used);
    uart_puts(" bytes (");
    uart_puthex((early_pool->used * 100) / early_pool->size);
    uart_puts("%)\n");
    uart_puts("Free:            ");
    uart_puthex(early_pool->size - early_pool->used);
    uart_puts(" bytes\n");
    uart_puts("Peak usage:      ");
    uart_puthex(early_pool->peak_usage);
    uart_puts(" bytes\n");
    uart_puts("Allocations:     ");
    uart_puthex(early_pool->allocations);
    uart_puts("\n");
    
    uart_puts("\nAllocation breakdown:\n");
    uart_puts("  Devices:       ");
    uart_puthex(early_stats.device_allocs);
    uart_puts(" (");
    uart_puthex(early_stats.device_allocs * sizeof(struct device));
    uart_puts(" bytes)\n");
    uart_puts("  Resources:     ");
    uart_puthex(early_stats.resource_allocs);
    uart_puts(" (");
    uart_puthex(early_stats.resource_allocs * sizeof(struct resource));
    uart_puts(" bytes)\n");
    uart_puts("  Strings:       ");
    uart_puthex(early_stats.string_allocs);
    uart_puts("\n");
    uart_puts("  Other:         ");
    uart_puthex(early_stats.misc_allocs);
    uart_puts("\n");
}

/* Check pool integrity */
bool early_pool_check_integrity(void) {
    if (!early_pool_initialized) {
        return false;
    }
    
    /* Check magic number */
    if (early_pool->magic != EARLY_POOL_MAGIC) {
        uart_puts("EARLY_POOL: ERROR - Invalid magic number\n");
        return false;
    }
    
    /* Check bounds */
    if (early_pool->used > early_pool->size) {
        uart_puts("EARLY_POOL: ERROR - Pool overflow detected\n");
        return false;
    }
    
    /* Check next pointer */
    uint8_t *expected_next = (uint8_t *)early_pool + early_pool->used;
    if (early_pool->next != expected_next) {
        uart_puts("EARLY_POOL: ERROR - Next pointer corruption\n");
        return false;
    }
    
    return true;
}

/* Reset pool (for testing only) */
void early_pool_reset(void) {
    if (!early_pool_initialized) {
        return;
    }
    
    /* Clear statistics */
    memset(&early_stats, 0, sizeof(early_stats));
    
    /* Re-initialize the pool */
    early_pool_initialized = false;
    early_pool_init();
}