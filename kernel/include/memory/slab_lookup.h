/*
 * kernel/include/memory/slab_lookup.h
 *
 * Hash table for fast slab address lookup (O(1) average case)
 * Supports unlimited scaling with dynamic growth
 */

#ifndef __SLAB_LOOKUP_H__
#define __SLAB_LOOKUP_H__

#include <stdint.h>
#include <stdbool.h>
#include <memory/slab.h>

// Forward declarations
struct kmem_cache;
struct kmem_slab;

// Hash table entry for slab tracking
struct slab_hash_entry {
    struct slab_hash_entry *next;    // Collision chain
    uintptr_t page_addr;             // Page address this entry represents
    uintptr_t start_addr;            // Start of slab memory
    uintptr_t end_addr;              // End of slab memory  
    struct kmem_cache *cache;        // Owning cache
    struct kmem_slab *slab;          // Slab metadata
};

// Hash table bucket
struct slab_hash_bucket {
    struct slab_hash_entry *head;    // Chain head
    // Future: add per-bucket lock for fine-grained locking
};

// Bootstrap allocation tracking
struct bootstrap_alloc {
    void *addr;
    size_t size;
};

// Main lookup structure
struct slab_lookup {
    struct slab_hash_bucket *buckets;  // Array of buckets
    size_t num_buckets;                // Current table size
    size_t num_entries;                // Total entries
    size_t hash_mask;                  // For fast modulo (num_buckets - 1)
    
    // Growth management
    size_t resize_threshold;           // When to grow
    bool bootstrap_mode;               // True until kmalloc is ready
    bool resizing;                     // Currently resizing (prevent recursion)
    
    // Bootstrap allocations tracking
    struct bootstrap_alloc bootstrap_allocs[16];  // Track early PMM allocations
    size_t num_bootstrap_allocs;
    
    // Statistics
    uint64_t lookups;
    uint64_t collisions;  
    uint64_t rehashes;
    
    // Future: spinlock_t lock;
};

// Public API
void slab_lookup_init(void);
void slab_lookup_insert(struct kmem_slab *slab);
void slab_lookup_remove(struct kmem_slab *slab);
struct kmem_cache *slab_lookup_find(void *addr);
void slab_lookup_migrate_to_dynamic(void);

// Statistics and debugging
void slab_lookup_dump_stats(void);
size_t slab_lookup_get_entry_count(void);
uint32_t slab_lookup_get_load_factor_percent(void);

#endif /* __SLAB_LOOKUP_H__ */