/*
 * kernel/memory/kmalloc.c
 *
 * Kernel memory allocator implementation
 */

#include <memory/kmalloc.h>
#include <memory/size_classes.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <memory/vmm.h>
#include <memory/malloc_types.h>
#include <memory/slab_lookup.h>
#include <uart.h>
#include <string.h>

// Debug printing
#define KMALLOC_DEBUG 0

#if KMALLOC_DEBUG
#define kmalloc_debug(msg) uart_puts("[KMALLOC] " msg)
#else
#define kmalloc_debug(msg)
#endif

// Slab caches for each size class
static struct kmem_cache *size_caches[KMALLOC_NUM_CLASSES];
static int kmalloc_initialized = 0;

// Global statistics
static struct kmalloc_stats global_stats = {0};

// Initialize kmalloc subsystem
void kmalloc_init(void) {
    if (kmalloc_initialized) {
        kmalloc_debug("Already initialized\n");
        return;
    }
    
    // Initialize hash table for slab lookups (uses PMM bootstrap)
    slab_lookup_init();
    
    // Initialize malloc type system first
    malloc_type_init();
    
    // Make sure slab allocator is initialized
    kmem_init();
    
    // Create slab caches for each size class
    for (int i = 0; i < KMALLOC_NUM_CLASSES; i++) {
        size_t size = kmalloc_size_classes[i];
        char name[32];
        
        // Format cache name
        if (size < 1024) {
            snprintf(name, sizeof(name), "kmalloc-%zu", size);
        } else {
            snprintf(name, sizeof(name), "kmalloc-%zuK", size / 1024);
        }
        
        // Create cache with appropriate alignment
        size_t align = (size >= 256) ? 64 : 16;
        
        // Add red zone size if debugging
        size_t actual_size = size + (2 * KMALLOC_REDZONE_SIZE);
        
        size_caches[i] = kmem_cache_create(name, actual_size, align, 0);
        
        if (!size_caches[i]) {
            uart_puts("[KMALLOC] Failed to create cache for size ");
            uart_putdec(size);
            uart_puts("\n");
            // Continue with other caches
        }
    }
    
    // Mark as initialized BEFORE migration to prevent recursion
    kmalloc_initialized = 1;
    
    // NOW kmalloc is functional - migrate hash table to dynamic memory
    slab_lookup_migrate_to_dynamic();
    
    kmalloc_debug("Kmalloc initialized with zero-overhead design\n");
}

// Find which cache owns an address by searching all slabs
struct kmem_cache *kmalloc_find_cache(void *ptr) {
    // Use the slab allocator's API to find the cache
    return kmem_find_cache_for_object(ptr);
}

// Main allocation function 
void *kmalloc(size_t size, int flags) {
    if (!kmalloc_initialized) {
        kmalloc_init();
    }
    
    // Handle zero-size allocations
    if (size == 0) {
        return NULL;
    }
    
    // Check if this is a large allocation
    if (kmalloc_is_large(size)) {
        // Large allocation - needs header
        size_t total_size = size + KMALLOC_LARGE_HEADER_SIZE;
        size_t pages_needed = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
        
        kmalloc_debug("Large allocation: size=");
        if (KMALLOC_DEBUG) {
            uart_putdec(size);
            uart_puts(" pages=");
            uart_putdec(pages_needed);
            uart_puts("\n");
        }
        
        uint64_t phys_addr = pmm_alloc_pages(pages_needed);
        
        if (!phys_addr) {
            global_stats.failed_allocs++;
            return NULL;
        }
        
        // Convert to virtual address using DMAP
        void *virt_addr = (void *)PHYS_TO_DMAP(phys_addr);
        if (!virt_addr) {
            pmm_free_pages(phys_addr, pages_needed);
            global_stats.failed_allocs++;
            return NULL;
        }
        
        // Set up header
        struct kmalloc_large_header *header = (struct kmalloc_large_header *)virt_addr;
        header->size = size;
        header->magic = KMALLOC_LARGE_MAGIC;
        header->flags = flags;
        
        // Update statistics
        global_stats.total_allocs++;
        global_stats.active_allocs++;
        global_stats.total_bytes += size;
        global_stats.active_bytes += size;
        global_stats.large_allocs++;
        global_stats.large_bytes += size;
        
        // Return pointer after header
        return (char *)virt_addr + KMALLOC_LARGE_HEADER_SIZE;
    }
    
    // Find appropriate size class
    int class = kmalloc_size_to_class(size);
    if (class < 0) {
        // This should have been caught by kmalloc_is_large check above
        uart_puts("[KMALLOC] ERROR: size ");
        uart_putdec(size);
        uart_puts(" not handled by is_large check but has no size class\n");
        global_stats.failed_allocs++;
        return NULL;
    }
    if (!size_caches[class]) {
        global_stats.failed_allocs++;
        return NULL;
    }
    
    // Allocate from slab cache - NO HEADER!
    void *obj = kmem_cache_alloc(size_caches[class], flags);
    if (!obj) {
        global_stats.failed_allocs++;
        return NULL;
    }
    
#ifdef KMALLOC_DEBUG
    // Fill red zones in debug mode
    if (KMALLOC_REDZONE_SIZE > 0) {
        memset(obj, KMALLOC_REDZONE_MAGIC, KMALLOC_REDZONE_SIZE);
        memset((char *)obj + KMALLOC_REDZONE_SIZE + size, 
               KMALLOC_REDZONE_MAGIC, KMALLOC_REDZONE_SIZE);
        obj = (char *)obj + KMALLOC_REDZONE_SIZE;
    }
#endif
    
    // Zero memory if requested
    if (flags & KM_ZERO) {
        memset(obj, 0, size);
    }
    
    // Update statistics
    global_stats.total_allocs++;
    global_stats.active_allocs++;
    global_stats.total_bytes += size;
    global_stats.active_bytes += size;
    
    // Return pointer directly - no header!
    return obj;
}

// Free allocated memory
void kfree(void *ptr) {
    if (!ptr) {
        return;
    }
    
    // First, try to find in slab caches
    struct kmem_cache *cache = kmalloc_find_cache(ptr);
    
    if (cache) {
        // It's a slab allocation
        void *obj_to_free = ptr;
        
#ifdef KMALLOC_DEBUG
        // Adjust for red zone
        if (KMALLOC_REDZONE_SIZE > 0) {
            obj_to_free = (char *)ptr - KMALLOC_REDZONE_SIZE;
        }
#endif
        
        // Get the size from the cache
        size_t obj_size = cache->object_size - (2 * KMALLOC_REDZONE_SIZE);
        
        // Update statistics
        global_stats.total_frees++;
        global_stats.active_allocs--;
        global_stats.active_bytes -= obj_size;
        
        // Free to cache
        kmem_cache_free(cache, obj_to_free);
    } else {
        // Must be a large allocation - check header
        struct kmalloc_large_header *header = 
            (struct kmalloc_large_header *)((char *)ptr - KMALLOC_LARGE_HEADER_SIZE);
        
        // Validate magic
        if (header->magic != KMALLOC_LARGE_MAGIC) {
            uart_puts("[KMALLOC] Invalid magic in large free: ");
            uart_puthex(header->magic);
            uart_puts("\n");
            return;
        }
        
        // Mark as freed
        size_t size = header->size;
        header->magic = KMALLOC_LARGE_FREE;
        
        // Update statistics
        global_stats.total_frees++;
        global_stats.active_allocs--;
        global_stats.active_bytes -= size;
        global_stats.large_bytes -= size;
        
        // Free pages
        size_t total_size = size + KMALLOC_LARGE_HEADER_SIZE;
        size_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t phys_addr = DMAP_TO_PHYS((uint64_t)header);
        
        if (phys_addr) {
            pmm_free_pages(phys_addr, pages);
        }
    }
}

// Get allocation size
size_t kmalloc_size(void *ptr) {
    if (!ptr) {
        return 0;
    }
    
    // Try to find in slab caches
    struct kmem_cache *cache = kmalloc_find_cache(ptr);
    
    if (cache) {
        // Return the usable size (minus red zones)
        return cache->object_size - (2 * KMALLOC_REDZONE_SIZE);
    } else {
        // Check large allocation header
        struct kmalloc_large_header *header = 
            (struct kmalloc_large_header *)((char *)ptr - KMALLOC_LARGE_HEADER_SIZE);
        
        if (header->magic == KMALLOC_LARGE_MAGIC) {
            return header->size;
        }
    }
    
    return 0;
}

// Reallocate memory
void *krealloc(void *ptr, size_t new_size, int flags) {
    // Handle special cases
    if (!ptr) {
        return kmalloc(new_size, flags);
    }
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    // Get current size
    size_t old_size = kmalloc_size(ptr);
    if (old_size == 0) {
        return NULL;
    }
    
    // If new size fits in current allocation, just return
    if (new_size <= old_size) {
        // Note: We can't shrink the allocation, so we keep the same size
        return ptr;
    }
    
    // Need to allocate new memory
    void *new_ptr = kmalloc(new_size, flags);
    if (!new_ptr) {
        return NULL;
    }
    
    // Copy old data
    memcpy(new_ptr, ptr, old_size);
    
    // Free old memory
    kfree(ptr);
    
    return new_ptr;
}

// Allocate zeroed memory for array
void *kcalloc(size_t nmemb, size_t size, int flags) {
    // Check for overflow
    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size) {
        return NULL;
    }
    
    return kmalloc(total, flags | KM_ZERO);
}

// Validate allocation
int kmalloc_validate(void *ptr) {
    if (!ptr) {
        return 0;
    }
    
    // Check if it's in a slab cache
    struct kmem_cache *cache = kmalloc_find_cache(ptr);
    if (cache) {
        return 1;  // Valid slab allocation
    }
    
    // Check large allocation
    struct kmalloc_large_header *header = 
        (struct kmalloc_large_header *)((char *)ptr - KMALLOC_LARGE_HEADER_SIZE);
    
    return header->magic == KMALLOC_LARGE_MAGIC;
}

// Typed allocation functions
void *kmalloc_type(size_t size, struct malloc_type *type, int flags) {
    void *ptr;
    
    // Use default type if none specified
    if (!type) {
        type = &M_KMALLOC;
    }
    
    // Allocate memory
    ptr = kmalloc(size, flags);
    
    // Update type statistics on success
    if (ptr) {
        malloc_type_update_alloc(type, size);
    } else {
        // Update failed allocation count
        if (type) {
            type->stats.failed_allocs++;
        }
    }
    
    return ptr;
}

void kfree_type(void *ptr, struct malloc_type *type) {
    size_t size;
    
    if (!ptr) {
        return;
    }
    
    // Use default type if none specified (same as kmalloc_type)
    if (!type) {
        type = &M_KMALLOC;
    }
    
    // Get the size before freeing
    size = kmalloc_size(ptr);
    
    // Free the memory
    kfree(ptr);
    
    // Update type statistics
    if (type && size > 0) {
        malloc_type_update_free(type, size);
    }
}

// Get statistics
void kmalloc_get_stats(struct kmalloc_stats *stats) {
    if (stats) {
        *stats = global_stats;
    }
}

// Dump statistics
void kmalloc_dump_stats(void) {
    uart_puts("\n=== Kmalloc Statistics ===\n");
    uart_puts("Total allocations: ");
    uart_putdec(global_stats.total_allocs);
    uart_puts("\nTotal frees: ");
    uart_putdec(global_stats.total_frees);
    uart_puts("\nActive allocations: ");
    uart_putdec(global_stats.active_allocs);
    uart_puts("\nTotal bytes allocated: ");
    uart_putdec(global_stats.total_bytes);
    uart_puts("\nActive bytes: ");
    uart_putdec(global_stats.active_bytes);
    uart_puts("\nLarge allocations: ");
    uart_putdec(global_stats.large_allocs);
    uart_puts("\nLarge bytes: ");
    uart_putdec(global_stats.large_bytes);
    uart_puts("\nFailed allocations: ");
    uart_putdec(global_stats.failed_allocs);
    uart_puts("\n\n");
    
    // Dump per-cache statistics
    uart_puts("=== Per-Size Cache Statistics ===\n");
    for (int i = 0; i < KMALLOC_NUM_CLASSES; i++) {
        if (size_caches[i]) {
            kmem_cache_dump(size_caches[i]);
        }
    }
    
    // Dump malloc type statistics
    malloc_type_dump_stats();
}
