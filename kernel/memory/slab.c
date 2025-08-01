/*
 * kernel/memory/slab.c
 *
 * Slab allocator implementation
 * Based on the design principles from FreeBSD's malloc(9)
 */

#include <memory/slab.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <memory/vmm.h>
#include <uart.h>
#include <string.h>
#include <stddef.h>

// Debug printing - disabled by default for production
#define SLAB_DEBUG 0

#if SLAB_DEBUG
// Debug printing helpers
static void slab_print_string(const char *str) {
    uart_puts(str);
}

static void slab_print_number(uint64_t num) {
    uart_putdec(num);
}

static void slab_print_hex(uint64_t num) {
    uart_puthex(num);
}

#define slab_debug(msg) slab_print_string("[SLAB] " msg)
#else
#define slab_debug(msg)
#endif

// Constants
#define SLAB_MAGIC 0xDEADBEEF
#define MIN_SLAB_SIZE PAGE_SIZE      // Minimum slab size is one page
#define MAX_SLAB_SIZE (64 * PAGE_SIZE) // Maximum slab size is 64 pages

// Global list of all caches (for debugging/statistics)
static struct slab_list_node cache_list;
static int slab_initialized = 0;

// Helper function to align value up
static inline size_t align_up(size_t value, size_t align) {
    return (value + align - 1) & ~(align - 1);
}

// Helper function to get object address from index
static inline void *slab_get_object(struct kmem_slab *slab, uint32_t index) {
    return (char *)slab->slab_base + (index * slab->cache->object_size);
}

// Helper function to get object index from address
static inline uint32_t slab_get_index(struct kmem_slab *slab, void *obj) {
    return ((char *)obj - (char *)slab->slab_base) / slab->cache->object_size;
}

// Initialize the slab allocator subsystem
void kmem_init(void) {
    if (slab_initialized) {
        slab_debug("Already initialized\n");
        return;
    }
    
    SLAB_LIST_INIT(&cache_list);
    slab_initialized = 1;
    
    slab_debug("Slab allocator initialized\n");
}

// Calculate optimal slab size and objects per slab
static void calculate_slab_parameters(struct kmem_cache *cache) {
    size_t obj_size = cache->object_size;
    size_t slab_size = MIN_SLAB_SIZE;
    
    // Start with minimum slab size and increase if needed
    while (slab_size < MAX_SLAB_SIZE) {
        // Calculate space needed for slab header and alignment padding
        size_t metadata_size = sizeof(struct kmem_slab);
        // Account for worst-case alignment padding
        metadata_size = align_up(metadata_size, cache->align);
        size_t available = slab_size - metadata_size;
        
        // Calculate how many objects fit
        size_t num_objs = available / (obj_size + sizeof(uint32_t));
        
        // We want at least 8 objects per slab for efficiency
        if (num_objs >= 8) {
            cache->objects_per_slab = num_objs;
            return;
        }
        
        // Try next slab size
        slab_size *= 2;
    }
    
    // Use maximum slab size
    size_t metadata_size = sizeof(struct kmem_slab);
    metadata_size = align_up(metadata_size, cache->align);
    size_t available = MAX_SLAB_SIZE - metadata_size;
    cache->objects_per_slab = available / (obj_size + sizeof(uint32_t));
}

// Allocate and initialize a new slab
static struct kmem_slab *slab_create(struct kmem_cache *cache) {
    // Calculate slab size
    size_t slab_size = MIN_SLAB_SIZE;
    size_t needed = sizeof(struct kmem_slab) + 
                   (cache->objects_per_slab * cache->object_size) +
                   (cache->objects_per_slab * sizeof(uint32_t));
    
    while (slab_size < needed && slab_size < MAX_SLAB_SIZE) {
        slab_size *= 2;
    }
    
    // Allocate pages from PMM
    size_t num_pages = slab_size / PAGE_SIZE;
    uint64_t phys_addr = pmm_alloc_pages(num_pages);
    if (phys_addr == 0) {
        slab_debug("Failed to allocate pages for slab\n");
        return NULL;
    }
    
    // Convert to virtual address using DMAP
    void *slab_mem = (void *)PHYS_TO_DMAP(phys_addr);
    
    // Initialize slab structure at the beginning
    struct kmem_slab *slab = (struct kmem_slab *)slab_mem;
    // Align slab_base according to cache alignment requirement
    void *base_addr = (char *)slab_mem + sizeof(struct kmem_slab);
    slab->slab_base = (void *)align_up((uintptr_t)base_addr, cache->align);
    slab->slab_size = slab_size;
    slab->num_objects = cache->objects_per_slab;
    slab->num_free = cache->objects_per_slab;
    slab->cache = cache;
    
    // Initialize freelist at the end of objects
    slab->freelist = (uint32_t *)((char *)slab->slab_base + 
                                  (cache->objects_per_slab * cache->object_size));
    
    // Initialize freelist as a linked list of indices
    for (uint32_t i = 0; i < cache->objects_per_slab; i++) {
        slab->freelist[i] = i + 1;
    }
    slab->freelist[cache->objects_per_slab - 1] = 0xFFFFFFFF; // End marker
    slab->freelist_head = 0;
    
    // Update cache statistics
    cache->stats.total_slabs++;
    cache->stats.total_objs += cache->objects_per_slab;
    
    slab_debug("Created slab for cache\n");
    
    return slab;
}

// Free a slab back to PMM
static void slab_destroy(struct kmem_slab *slab) {
    struct kmem_cache *cache = slab->cache;
    
    // Update statistics
    cache->stats.total_slabs--;
    cache->stats.total_objs -= slab->num_objects;
    
    // Convert back to physical address and free
    uint64_t phys_addr = DMAP_TO_PHYS((uint64_t)slab);
    size_t num_pages = slab->slab_size / PAGE_SIZE;
    pmm_free_pages(phys_addr, num_pages);
    
    slab_debug("Destroyed slab\n");
}

// Create a new slab cache
struct kmem_cache *kmem_cache_create(const char *name, size_t size, 
                                    size_t align, unsigned long flags) {
    if (!slab_initialized) {
        slab_debug("Slab allocator not initialized\n");
        return NULL;
    }
    
    if (size == 0 || size > MAX_SLAB_SIZE / 2) {
        slab_debug("Invalid object size\n");
        return NULL;
    }
    
    // Align object size
    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }
    size = align_up(size, align);
    
    // Allocate cache structure (bootstrap: use PMM directly)
    uint64_t cache_phys = pmm_alloc_pages(1);
    if (cache_phys == 0) {
        slab_debug("Failed to allocate cache structure\n");
        return NULL;
    }
    
    struct kmem_cache *cache = (struct kmem_cache *)PHYS_TO_DMAP(cache_phys);
    
    // Initialize cache
    memset(cache, 0, sizeof(*cache));
    strncpy(cache->name, name, sizeof(cache->name) - 1);
    cache->object_size = size;
    cache->align = align;
    cache->flags = flags;
    
    // Initialize lists
    SLAB_LIST_INIT(&cache->full_slabs);
    SLAB_LIST_INIT(&cache->partial_slabs);
    SLAB_LIST_INIT(&cache->empty_slabs);
    
    // Calculate slab parameters
    calculate_slab_parameters(cache);
    
    // Add to global cache list
    SLAB_LIST_INSERT_HEAD(&cache_list, &cache->cache_link);
    
    slab_debug("Created cache\n");
    
    return cache;
}

// Destroy a slab cache
void kmem_cache_destroy(struct kmem_cache *cache) {
    if (!cache) return;
    
    slab_debug("Destroying cache\n");
    
    // Free all slabs
    struct slab_list_node *node, *next;
    
    // Free full slabs
    for (node = cache->full_slabs.next; node != &cache->full_slabs; node = next) {
        next = node->next;
        struct kmem_slab *slab = (struct kmem_slab *)node;
        SLAB_LIST_REMOVE(node);
        slab_destroy(slab);
    }
    
    // Free partial slabs
    for (node = cache->partial_slabs.next; node != &cache->partial_slabs; node = next) {
        next = node->next;
        struct kmem_slab *slab = (struct kmem_slab *)node;
        SLAB_LIST_REMOVE(node);
        slab_destroy(slab);
    }
    
    // Free empty slabs
    for (node = cache->empty_slabs.next; node != &cache->empty_slabs; node = next) {
        next = node->next;
        struct kmem_slab *slab = (struct kmem_slab *)node;
        SLAB_LIST_REMOVE(node);
        slab_destroy(slab);
    }
    
    // Remove from global cache list
    SLAB_LIST_REMOVE(&cache->cache_link);
    
    // Free cache structure
    uint64_t cache_phys = DMAP_TO_PHYS((uint64_t)cache);
    pmm_free_pages(cache_phys, 1);
}

// Allocate an object from a cache
void *kmem_cache_alloc(struct kmem_cache *cache, int flags) {
    if (!cache) return NULL;
    
    struct kmem_slab *slab = NULL;
    
    // Try partial slabs first
    if (!SLAB_LIST_EMPTY(&cache->partial_slabs)) {
        slab = (struct kmem_slab *)cache->partial_slabs.next;
    }
    // Then try empty slabs
    else if (!SLAB_LIST_EMPTY(&cache->empty_slabs)) {
        slab = (struct kmem_slab *)cache->empty_slabs.next;
        SLAB_LIST_REMOVE(&slab->slab_link);
        SLAB_LIST_INSERT_HEAD(&cache->partial_slabs, &slab->slab_link);
        cache->stats.active_slabs++;
    }
    // Need to allocate a new slab
    else {
        slab = slab_create(cache);
        if (!slab) {
            if (!(flags & KM_NOSLEEP)) {
                // Could wait/retry here in the future
            }
            return NULL;
        }
        SLAB_LIST_INSERT_HEAD(&cache->partial_slabs, &slab->slab_link);
        cache->stats.active_slabs++;
    }
    
    // Allocate object from slab
    if (slab->num_free == 0) {
        slab_debug("Error: slab has no free objects\n");
        return NULL;
    }
    
    uint32_t index = slab->freelist_head;
    slab->freelist_head = slab->freelist[index];
    slab->num_free--;
    
    void *obj = slab_get_object(slab, index);
    
    // Move to full list if no more free objects
    if (slab->num_free == 0) {
        SLAB_LIST_REMOVE(&slab->slab_link);
        SLAB_LIST_INSERT_HEAD(&cache->full_slabs, &slab->slab_link);
    }
    
    // Zero memory if requested
    if (flags & KM_ZERO) {
        memset(obj, 0, cache->object_size);
    }
    
    // Update statistics
    cache->stats.allocs++;
    cache->stats.active_objs++;
    
    return obj;
}

// Free an object back to its cache
void kmem_cache_free(struct kmem_cache *cache, void *obj) {
    if (!cache || !obj) return;
    
    // Find which slab this object belongs to
    struct kmem_slab *slab = NULL;
    struct slab_list_node *node;
    
    // Search in full slabs
    for (node = cache->full_slabs.next; node != &cache->full_slabs; node = node->next) {
        struct kmem_slab *s = (struct kmem_slab *)node;
        if (obj >= s->slab_base && obj < (char *)s->slab_base + 
            (s->num_objects * cache->object_size)) {
            slab = s;
            break;
        }
    }
    
    // Search in partial slabs
    if (!slab) {
        for (node = cache->partial_slabs.next; node != &cache->partial_slabs; node = node->next) {
            struct kmem_slab *s = (struct kmem_slab *)node;
            if (obj >= s->slab_base && obj < (char *)s->slab_base + 
                (s->num_objects * cache->object_size)) {
                slab = s;
                break;
            }
        }
    }
    
    if (!slab) {
        slab_debug("Error: object not found in cache\n");
        return;
    }
    
    // Get object index and add to freelist
    uint32_t index = slab_get_index(slab, obj);
    slab->freelist[index] = slab->freelist_head;
    slab->freelist_head = index;
    slab->num_free++;
    
    // Update statistics
    cache->stats.frees++;
    cache->stats.active_objs--;
    
    // Move slab between lists if needed
    if (slab->num_free == 1) {
        // Check if this slab was in the full list
        struct slab_list_node *check_node;
        for (check_node = cache->full_slabs.next; check_node != &cache->full_slabs; check_node = check_node->next) {
            if ((struct kmem_slab *)check_node == slab) {
                // Was full, now partial
                SLAB_LIST_REMOVE(&slab->slab_link);
                SLAB_LIST_INSERT_HEAD(&cache->partial_slabs, &slab->slab_link);
                break;
            }
        }
    } else if (slab->num_free == slab->num_objects) {
        // Now empty
        SLAB_LIST_REMOVE(&slab->slab_link);
        SLAB_LIST_INSERT_HEAD(&cache->empty_slabs, &slab->slab_link);
        cache->stats.active_slabs--;
        
        // Optionally destroy empty slabs if NOREAP not set
        if (!(cache->flags & KMEM_CACHE_NOREAP)) {
            // Keep at least one empty slab
            if (!SLAB_LIST_EMPTY(&cache->empty_slabs) && 
                cache->empty_slabs.next->next != &cache->empty_slabs) {
                slab_destroy(slab);
            }
        }
    }
}

// Get cache statistics
void kmem_cache_stats(struct kmem_cache *cache, struct kmem_stats *stats) {
    if (!cache || !stats) return;
    *stats = cache->stats;
}

// Debug: dump cache information
void kmem_cache_dump(struct kmem_cache *cache) {
    if (!cache) return;
    
    uart_puts("\nCache: ");
    uart_puts(cache->name);
    uart_puts("\n  Object size: ");
    uart_putdec(cache->object_size);
    uart_puts(", Align: ");
    uart_putdec(cache->align);
    uart_puts("\n  Objects per slab: ");
    uart_putdec(cache->objects_per_slab);
    uart_puts("\n  Statistics:\n");
    uart_puts("    Allocations: ");
    uart_putdec(cache->stats.allocs);
    uart_puts("\n    Frees: ");
    uart_putdec(cache->stats.frees);
    uart_puts("\n    Active objects: ");
    uart_putdec(cache->stats.active_objs);
    uart_puts("\n    Total objects: ");
    uart_putdec(cache->stats.total_objs);
    uart_puts("\n    Active slabs: ");
    uart_putdec(cache->stats.active_slabs);
    uart_puts("\n    Total slabs: ");
    uart_putdec(cache->stats.total_slabs);
    uart_puts("\n");
    
    // Count slabs in each list
    int full = 0, partial = 0, empty = 0;
    struct slab_list_node *node;
    
    for (node = cache->full_slabs.next; node != &cache->full_slabs; node = node->next)
        full++;
    for (node = cache->partial_slabs.next; node != &cache->partial_slabs; node = node->next)
        partial++;
    for (node = cache->empty_slabs.next; node != &cache->empty_slabs; node = node->next)
        empty++;
    
    uart_puts("  Slab lists: ");
    uart_putdec(full);
    uart_puts(" full, ");
    uart_putdec(partial);
    uart_puts(" partial, ");
    uart_putdec(empty);
    uart_puts(" empty\n");
}

// Debug: dump all caches
void kmem_dump_all_caches(void) {
    if (!slab_initialized) {
        uart_puts("Slab allocator not initialized\n");
        return;
    }
    
    uart_puts("\n=== Slab Cache Dump ===\n");
    
    struct slab_list_node *node;
    for (node = cache_list.next; node != &cache_list; node = node->next) {
        // Get the cache from the list node using container_of pattern
        struct kmem_cache *cache = (struct kmem_cache *)((char *)node - offsetof(struct kmem_cache, cache_link));
        kmem_cache_dump(cache);
    }
    
    uart_puts("======================\n");
}

// Reap unused memory from caches (future enhancement)
void kmem_cache_reap(void) {
    // TODO: Implement cache reaping
    // For now, this is a no-op
}