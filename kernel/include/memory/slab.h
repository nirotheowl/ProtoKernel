/*
 * kernel/include/memory/slab.h
 *
 * Slab allocator interface
 * Based on the design principles from FreeBSD's malloc(9)
 */

#ifndef _SLAB_H_
#define _SLAB_H_

#include <stdint.h>
#include <stddef.h>

// Forward declarations
struct kmem_cache;
struct kmem_slab;

// List node structure for linking slabs
struct slab_list_node {
    struct slab_list_node *next;
    struct slab_list_node *prev;
};

// Statistics for a slab cache
struct kmem_stats {
    uint64_t allocs;        // Total allocations
    uint64_t frees;         // Total frees
    uint64_t active_objs;   // Currently allocated objects
    uint64_t total_objs;    // Total objects in all slabs
    uint64_t active_slabs;  // Number of slabs with allocated objects
    uint64_t total_slabs;   // Total number of slabs
};

// Slab structure - manages a contiguous block of memory for fixed-size objects
struct kmem_slab {
    struct slab_list_node slab_link;  // Link in cache's slab lists
    void *slab_base;                  // Start of slab memory
    size_t slab_size;                 // Total size of slab
    uint32_t num_objects;             // Total objects in slab
    uint32_t num_free;                // Free objects count
    uint32_t *freelist;               // Free object indices
    uint32_t freelist_head;           // Index of first free object
    struct kmem_cache *cache;         // Parent cache
};

// Slab cache structure - manages slabs of a specific object size
struct kmem_cache {
    struct slab_list_node cache_link;     // Link in global cache list
    char name[32];                    // Cache name
    size_t object_size;               // Size of each object
    size_t align;                     // Alignment requirement
    uint32_t objects_per_slab;        // Objects per slab
    uint32_t flags;                   // Cache flags
    
    // Slab lists
    struct slab_list_node full_slabs;    // Fully allocated slabs
    struct slab_list_node partial_slabs;  // Partially full slabs
    struct slab_list_node empty_slabs;    // Empty slabs
    
    // Statistics
    struct kmem_stats stats;          // Cache statistics
    
    // Constructor/destructor (future enhancement)
    void (*ctor)(void *obj);          // Object constructor
    void (*dtor)(void *obj);          // Object destructor
};

// Cache flags
#define KMEM_CACHE_NOCPU      0x0001  // No per-CPU optimization
#define KMEM_CACHE_NODEBUG    0x0002  // No debugging features
#define KMEM_CACHE_NOREAP     0x0004  // Don't reap empty slabs
#define KMEM_CACHE_NOTRACK    0x0008  // Don't track in slab lookup (internal use)

// Allocation flags
#define KM_SLEEP      0x0001  // Can block/sleep
#define KM_NOSLEEP    0x0002  // Cannot block
#define KM_ZERO       0x0004  // Zero allocated memory
#define KM_NOWAIT     KM_NOSLEEP  // Alias for compatibility

// API Functions

// Initialize the slab allocator subsystem
void kmem_init(void);

// Create a new slab cache
struct kmem_cache *kmem_cache_create(const char *name, size_t size, 
                                    size_t align, unsigned long flags);

// Destroy a slab cache
void kmem_cache_destroy(struct kmem_cache *cache);

// Allocate an object from a cache
void *kmem_cache_alloc(struct kmem_cache *cache, int flags);

// Free an object back to its cache
void kmem_cache_free(struct kmem_cache *cache, void *obj);

// Get cache statistics
void kmem_cache_stats(struct kmem_cache *cache, struct kmem_stats *stats);

// Reap unused memory from caches (future enhancement)
void kmem_cache_reap(void);

// Debug/diagnostic functions
void kmem_cache_dump(struct kmem_cache *cache);
void kmem_dump_all_caches(void);

// Helper functions for kmalloc
struct kmem_slab *kmem_cache_find_slab(struct kmem_cache *cache, void *obj);
int kmem_cache_contains(struct kmem_cache *cache, void *obj);
struct kmem_cache *kmem_find_cache_for_object(void *obj);

// Helper macros for list operations
#define SLAB_LIST_INIT(head) do { \
    (head)->next = (struct slab_list_node *)(head); \
    (head)->prev = (struct slab_list_node *)(head); \
} while (0)

#define SLAB_LIST_EMPTY(head) ((head)->next == (struct slab_list_node *)(head))

#define SLAB_LIST_INSERT_HEAD(head, node) do { \
    (node)->next = (head)->next; \
    (node)->prev = (struct slab_list_node *)(head); \
    (head)->next->prev = (node); \
    (head)->next = (node); \
} while (0)

#define SLAB_LIST_REMOVE(node) do { \
    (node)->prev->next = (node)->next; \
    (node)->next->prev = (node)->prev; \
} while (0)

#endif /* _SLAB_H_ */