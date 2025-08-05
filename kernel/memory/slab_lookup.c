/*
 * kernel/memory/slab_lookup.c
 *
 * Hash table implementation for fast slab address lookup
 * Uses PMM for bootstrap allocation, then migrates to kmalloc
 */

#include <memory/slab_lookup.h>
#include <memory/slab.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <memory/kmalloc.h>
#include <memory/vmm.h>
#include <string.h>
#include <uart.h>
#include <stddef.h>

// Debug printing
#define SLAB_LOOKUP_DEBUG 0

#if SLAB_LOOKUP_DEBUG
#define lookup_debug(msg) uart_puts("[SLAB_LOOKUP] " msg)
#else
#define lookup_debug(msg)
#endif

// Global pointer (dynamically allocated)
static struct slab_lookup *g_slab_lookup = NULL;

// Bootstrap memory pool for hash entries during initialization
static uint8_t *bootstrap_pool = NULL;
static size_t bootstrap_offset = 0;

// Dedicated cache for hash entries (to avoid circular dependencies)
static struct kmem_cache *hash_entry_cache = NULL;

// Hash function - shift right by PAGE_SHIFT and mix bits
static inline uint32_t slab_hash(uintptr_t addr) {
    // Shift right by PAGE_SHIFT to ignore page offset
    addr >>= PAGE_SHIFT;
    
    // Mix bits for better distribution on 64-bit addresses
    addr ^= (addr >> 32);  // Mix high 32 bits with low 32 bits
    addr ^= (addr >> 16);
    addr ^= (addr >> 8);
    
    return addr & g_slab_lookup->hash_mask;
}

// Hash with custom mask (for resize operations)
static inline uint32_t slab_hash_with_mask(uintptr_t addr, size_t mask) {
    addr >>= PAGE_SHIFT;
    addr ^= (addr >> 32);
    addr ^= (addr >> 16);
    addr ^= (addr >> 8);
    return addr & mask;
}

// Initialize lookup system - BOOTSTRAP WITH PMM
void slab_lookup_init(void) {
    if (g_slab_lookup) {
        lookup_debug("Already initialized\n");
        return;
    }
    
    // Calculate initial size to handle bootstrap phase
    // We need ~14 size classes × 2 slabs each = 28 entries minimum
    // With 0.75 load factor, we need 28/0.75 = 38 buckets minimum
    // Round up to power of 2 for efficient masking
    size_t initial_buckets = 64;  // Handles up to 48 entries before resize
    size_t lookup_size = sizeof(struct slab_lookup);
    size_t buckets_size = sizeof(struct slab_hash_bucket) * initial_buckets;
    
    lookup_debug("Initializing with ");
    if (SLAB_LOOKUP_DEBUG) {
        uart_putdec(initial_buckets);
        uart_puts(" buckets\n");
    }
    
    // BOOTSTRAP: Use PMM directly for initial allocation
    // This is called before kmalloc is fully functional
    uint64_t lookup_phys = pmm_alloc_page();
    if (!lookup_phys) {
        uart_puts("[SLAB_LOOKUP] PANIC: Failed to allocate hash table\n");
        while (1) { }  // panic
    }
    
    g_slab_lookup = (struct slab_lookup *)PHYS_TO_DMAP(lookup_phys);
    memset(g_slab_lookup, 0, sizeof(struct slab_lookup));
    
    // Allocate bucket array with PMM
    size_t buckets_pages = (buckets_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t buckets_phys = pmm_alloc_pages(buckets_pages);
    if (!buckets_phys) {
        uart_puts("[SLAB_LOOKUP] PANIC: Failed to allocate hash buckets\n");
        while (1) { }  // panic
    }
    
    g_slab_lookup->buckets = (struct slab_hash_bucket *)PHYS_TO_DMAP(buckets_phys);
    memset(g_slab_lookup->buckets, 0, buckets_size);
    
    // Track bootstrap allocations for later cleanup
    g_slab_lookup->bootstrap_allocs[0].addr = g_slab_lookup;
    g_slab_lookup->bootstrap_allocs[0].size = PAGE_SIZE;
    g_slab_lookup->bootstrap_allocs[1].addr = g_slab_lookup->buckets;
    g_slab_lookup->bootstrap_allocs[1].size = buckets_pages * PAGE_SIZE;
    g_slab_lookup->num_bootstrap_allocs = 2;
    
    // Initialize fields
    g_slab_lookup->num_buckets = initial_buckets;
    g_slab_lookup->hash_mask = initial_buckets - 1;
    g_slab_lookup->resize_threshold = initial_buckets * 3 / 4;
    g_slab_lookup->bootstrap_mode = true;
    g_slab_lookup->num_entries = 0;
    g_slab_lookup->lookups = 0;
    g_slab_lookup->collisions = 0;
    g_slab_lookup->rehashes = 0;
    g_slab_lookup->resizing = false;
    
    lookup_debug("Hash table initialized in bootstrap mode\n");
}

// Allocate a hash entry during bootstrap
static struct slab_hash_entry *bootstrap_alloc_entry(void) {
    struct slab_hash_entry *entry;
    
    // Allocate a new page if needed
    if (!bootstrap_pool || bootstrap_offset + sizeof(*entry) > PAGE_SIZE) {
        uint64_t page = pmm_alloc_page();
        if (!page) {
            uart_puts("[SLAB_LOOKUP] PANIC: Bootstrap allocation failed\n");
            while (1) { }  // panic
        }
        bootstrap_pool = (uint8_t *)PHYS_TO_DMAP(page);
        bootstrap_offset = 0;
        
        // Track this allocation
        if (g_slab_lookup->num_bootstrap_allocs < 16) {
            g_slab_lookup->bootstrap_allocs[g_slab_lookup->num_bootstrap_allocs].addr = bootstrap_pool;
            g_slab_lookup->bootstrap_allocs[g_slab_lookup->num_bootstrap_allocs].size = PAGE_SIZE;
            g_slab_lookup->num_bootstrap_allocs++;
        }
    }
    
    entry = (struct slab_hash_entry *)(bootstrap_pool + bootstrap_offset);
    bootstrap_offset += sizeof(*entry);
    memset(entry, 0, sizeof(*entry));
    
    return entry;
}

// Dynamic resize when load factor too high
static void slab_lookup_resize(void) {
    struct slab_lookup *old_lookup = g_slab_lookup;
    struct slab_lookup *new_lookup;
    size_t new_size = old_lookup->num_buckets * 2;
    size_t i;
    
    lookup_debug("Resizing hash table from ");
    if (SLAB_LOOKUP_DEBUG) {
        uart_putdec(old_lookup->num_buckets);
        uart_puts(" to ");
        uart_putdec(new_size);
        uart_puts(" buckets (");
        uart_putdec(old_lookup->num_entries);
        uart_puts(" entries)\n");
    }
    
    // Should never resize during bootstrap - if we do, initial size was wrong
    if (old_lookup->bootstrap_mode) {
        uart_puts("[SLAB_LOOKUP] PANIC: Hash table resize required during bootstrap!\n");
        while (1) { }  // panic
    }
    
    // Set resizing flag to prevent recursion
    old_lookup->resizing = true;
    
    // Normal operation: grow without limits
    new_lookup = kmalloc(sizeof(struct slab_lookup), KM_ZERO);
    if (!new_lookup) {
        // If allocation fails, we continue with higher load factor
        // System may be under memory pressure - don't panic
        old_lookup->resize_threshold = old_lookup->num_buckets * 4;
        old_lookup->resizing = false;  // Clear flag
        lookup_debug("Resize failed - continuing with degraded performance\n");
        return;
    }
    
    new_lookup->num_buckets = new_size;
    new_lookup->hash_mask = new_size - 1;
    new_lookup->buckets = kmalloc(
        sizeof(struct slab_hash_bucket) * new_size,
        KM_ZERO
    );
    if (!new_lookup->buckets) {
        kfree(new_lookup);
        // Continue with degraded performance rather than fail
        old_lookup->resize_threshold = old_lookup->num_buckets * 4;
        old_lookup->resizing = false;  // Clear flag
        lookup_debug("Bucket allocation failed - continuing with degraded performance\n");
        return;
    }
    
    new_lookup->resize_threshold = new_size * 3 / 4;
    new_lookup->bootstrap_mode = false;
    new_lookup->resizing = false;  // Not resizing anymore
    
    // Rehash all entries
    for (i = 0; i < old_lookup->num_buckets; i++) {
        struct slab_hash_entry *entry = old_lookup->buckets[i].head;
        struct slab_hash_entry *next;
        
        while (entry) {
            next = entry->next;
            
            // Rehash into new table using the page address this entry represents
            uint32_t new_hash = slab_hash_with_mask(
                entry->page_addr, 
                new_lookup->hash_mask
            );
            entry->next = new_lookup->buckets[new_hash].head;
            new_lookup->buckets[new_hash].head = entry;
            
            entry = next;
        }
    }
    
    // Copy statistics and increment rehash count
    new_lookup->num_entries = old_lookup->num_entries;
    new_lookup->lookups = old_lookup->lookups;
    new_lookup->collisions = old_lookup->collisions;
    new_lookup->rehashes = old_lookup->rehashes + 1;
    
    // Atomic pointer swap (prepare for future SMP)
    g_slab_lookup = new_lookup;
    
    // Free old table
    kfree(old_lookup->buckets);
    kfree(old_lookup);
    
    lookup_debug("Resize completed successfully\n");
}

// Add slab to lookup table
void slab_lookup_insert(struct kmem_slab *slab) {
    if (!g_slab_lookup || !slab) {
        return;
    }
    
    // Calculate how many pages this slab spans
    uintptr_t start_addr = (uintptr_t)slab->slab_base;
    uintptr_t end_addr = start_addr + slab->slab_size;
    uintptr_t start_page = start_addr & ~(PAGE_SIZE - 1);
    uintptr_t end_page = (end_addr - 1) & ~(PAGE_SIZE - 1);
    size_t num_pages = ((end_page - start_page) / PAGE_SIZE) + 1;
    
    // Check if resize needed BEFORE inserting all pages
    // But don't resize if we're already in the middle of a resize (prevent recursion)
    if (!g_slab_lookup->resizing && 
        g_slab_lookup->num_entries + num_pages >= g_slab_lookup->resize_threshold) {
        slab_lookup_resize();
    }
    
    
    // Insert an entry for each page the slab spans
    for (uintptr_t page_addr = start_page; page_addr <= end_page; page_addr += PAGE_SIZE) {
        struct slab_hash_entry *entry;
        uint32_t hash;
        
        // Allocate entry - use PMM during bootstrap, dedicated cache after
        if (g_slab_lookup->bootstrap_mode) {
            entry = bootstrap_alloc_entry();
        } else {
            // Use dedicated cache to avoid circular dependencies
            entry = kmem_cache_alloc(hash_entry_cache, KM_ZERO);
            if (!entry) {
                // Silently fail - system can continue without tracking this slab
                return;
            }
        }
        
        entry->page_addr = page_addr;  // Store the page this entry represents
        entry->start_addr = start_addr;
        entry->end_addr = end_addr;
        entry->cache = slab->cache;
        entry->slab = slab;
        
        // Insert into hash table - hash based on the page address
        hash = slab_hash(page_addr);
        entry->next = g_slab_lookup->buckets[hash].head;
        g_slab_lookup->buckets[hash].head = entry;
        g_slab_lookup->num_entries++;
        
        lookup_debug("Inserted slab entry for page ");
        if (SLAB_LOOKUP_DEBUG) {
            uart_puthex(page_addr);
            uart_puts(" (slab ");
            uart_puthex(entry->start_addr);
            uart_puts(" - ");
            uart_puthex(entry->end_addr);
            uart_puts(", hash=");
            uart_putdec(hash);
            uart_puts(")\n");
        }
    }
}

// Remove slab from lookup table
void slab_lookup_remove(struct kmem_slab *slab) {
    if (!g_slab_lookup || !slab) {
        return;
    }
    
    // Calculate how many pages this slab spans
    uintptr_t start_addr = (uintptr_t)slab->slab_base;
    uintptr_t end_addr = start_addr + slab->slab_size;
    uintptr_t start_page = start_addr & ~(PAGE_SIZE - 1);
    uintptr_t end_page = (end_addr - 1) & ~(PAGE_SIZE - 1);
    
    // Remove entry for each page the slab spans
    for (uintptr_t page_addr = start_page; page_addr <= end_page; page_addr += PAGE_SIZE) {
        uint32_t hash = slab_hash(page_addr);
        struct slab_hash_entry *entry, *prev;
        
        // Search for entry in collision chain
        prev = NULL;
        for (entry = g_slab_lookup->buckets[hash].head; 
             entry != NULL; 
             prev = entry, entry = entry->next) {
            
            if (entry->slab == slab) {
                // Found it - remove from chain
                if (prev) {
                    prev->next = entry->next;
                } else {
                    g_slab_lookup->buckets[hash].head = entry->next;
                }
                
                // Free the entry if not in bootstrap mode
                if (!g_slab_lookup->bootstrap_mode) {
                    kmem_cache_free(hash_entry_cache, entry);
                }
                // Note: Bootstrap entries are never freed individually
                
                g_slab_lookup->num_entries--;
                
                lookup_debug("Removed slab entry for page ");
                if (SLAB_LOOKUP_DEBUG) {
                    uart_puthex(page_addr);
                    uart_puts("\n");
                }
                break;  // Found the entry for this page, move to next page
            }
        }
    }
}

// Find cache for address - O(1) average case
struct kmem_cache *slab_lookup_find(void *addr) {
    uint32_t hash;
    struct slab_hash_entry *entry;
    uintptr_t uaddr = (uintptr_t)addr;
    
    if (!g_slab_lookup || !addr) {
        return NULL;
    }
    
    // Hash based on the page containing the address
    uintptr_t page_addr = uaddr & ~(PAGE_SIZE - 1);
    hash = slab_hash(page_addr);
    
    // Update statistics
    g_slab_lookup->lookups++;
    
    // Search collision chain
    for (entry = g_slab_lookup->buckets[hash].head; 
         entry != NULL; 
         entry = entry->next) {
        
        if (uaddr >= entry->start_addr && uaddr < entry->end_addr) {
            // Found it!
            return entry->cache;
        }
        g_slab_lookup->collisions++;
    }
    
    return NULL;
}

// Migrate hash table from PMM bootstrap memory to dynamic allocations
void slab_lookup_migrate_to_dynamic(void) {
    struct slab_lookup *old_lookup;
    struct slab_lookup *new_lookup;
    size_t i;
    
    if (!g_slab_lookup || !g_slab_lookup->bootstrap_mode) {
        return;  // Already completed or not initialized
    }
    
    lookup_debug("Migrating hash table from bootstrap to dynamic memory\n");
    
    // Create dedicated cache for hash entries with NOTRACK flag
    // This prevents circular dependencies when allocating entries
    hash_entry_cache = kmem_cache_create("slab_hash_entries",
                                        sizeof(struct slab_hash_entry),
                                        8,
                                        KMEM_CACHE_NOTRACK | KMEM_CACHE_NOREAP);
    if (!hash_entry_cache) {
        uart_puts("[SLAB_LOOKUP] PANIC: Failed to create hash entry cache\n");
        while (1) { }  // panic
    }
    
    // Allocate new lookup structure with kmalloc
    new_lookup = kmalloc(sizeof(struct slab_lookup), KM_ZERO);
    if (!new_lookup) {
        uart_puts("[SLAB_LOOKUP] PANIC: Failed to migrate hash table\n");
        while (1) { }  // panic
    }
    
    // Copy all fields except the buckets pointer
    old_lookup = g_slab_lookup;
    *new_lookup = *old_lookup;
    
    // Allocate new bucket array with kmalloc
    new_lookup->buckets = kmalloc(
        sizeof(struct slab_hash_bucket) * new_lookup->num_buckets,
        KM_ZERO
    );
    if (!new_lookup->buckets) {
        uart_puts("[SLAB_LOOKUP] PANIC: Failed to migrate buckets\n");
        while (1) { }  // panic
    }
    
    // Migrate all hash entries to kmalloc'd memory
    for (i = 0; i < old_lookup->num_buckets; i++) {
        struct slab_hash_entry *old_entry = old_lookup->buckets[i].head;
        struct slab_hash_entry **new_chain = &new_lookup->buckets[i].head;
        
        while (old_entry) {
            // Allocate new entry with dedicated cache
            struct slab_hash_entry *new_entry = kmem_cache_alloc(hash_entry_cache, KM_ZERO);
            if (!new_entry) {
                uart_puts("[SLAB_LOOKUP] PANIC: Failed to migrate hash entry\n");
                while (1) { }  // panic
            }
            
            // Copy entry data
            new_entry->page_addr = old_entry->page_addr;
            new_entry->start_addr = old_entry->start_addr;
            new_entry->end_addr = old_entry->end_addr;
            new_entry->cache = old_entry->cache;
            new_entry->slab = old_entry->slab;
            new_entry->next = NULL;
            
            // Link into new chain
            *new_chain = new_entry;
            new_chain = &new_entry->next;
            
            old_entry = old_entry->next;
        }
    }
    
    // Clear bootstrap mode flag
    new_lookup->bootstrap_mode = false;
    
    // Atomic pointer swap
    g_slab_lookup = new_lookup;
    
    // Free all PMM pages used during bootstrap
    for (i = 0; i < old_lookup->num_bootstrap_allocs; i++) {
        uint64_t phys = DMAP_TO_PHYS((uintptr_t)old_lookup->bootstrap_allocs[i].addr);
        size_t pages = old_lookup->bootstrap_allocs[i].size / PAGE_SIZE;
        pmm_free_pages(phys, pages);
    }
    
    lookup_debug("Migration completed - ");
    if (SLAB_LOOKUP_DEBUG) {
        uart_putdec(old_lookup->num_bootstrap_allocs);
        uart_puts(" bootstrap pages freed, ");
        uart_putdec(new_lookup->num_entries);
        uart_puts(" entries migrated\n");
    }
}

// Get entry count
size_t slab_lookup_get_entry_count(void) {
    return g_slab_lookup ? g_slab_lookup->num_entries : 0;
}

// Get load factor as percentage (integer)
uint32_t slab_lookup_get_load_factor_percent(void) {
    if (!g_slab_lookup || g_slab_lookup->num_buckets == 0) {
        return 0;
    }
    return (g_slab_lookup->num_entries * 100) / g_slab_lookup->num_buckets;
}

// Dump statistics
void slab_lookup_dump_stats(void) {
    if (!g_slab_lookup) {
        uart_puts("Slab lookup not initialized\n");
        return;
    }
    
    uart_puts("\n=== Slab Lookup Statistics ===\n");
    uart_puts("Buckets: ");
    uart_putdec(g_slab_lookup->num_buckets);
    uart_puts("\nEntries: ");
    uart_putdec(g_slab_lookup->num_entries);
    uart_puts("\nLoad factor: ");
    
    // Print load factor as percentage
    uint32_t load_pct = (g_slab_lookup->num_entries * 100) / g_slab_lookup->num_buckets;
    uart_putdec(load_pct);
    uart_puts("%\n");
    
    uart_puts("Total lookups: ");
    uart_putdec(g_slab_lookup->lookups);
    uart_puts("\nCollisions: ");
    uart_putdec(g_slab_lookup->collisions);
    uart_puts("\nRehashes: ");
    uart_putdec(g_slab_lookup->rehashes);
    uart_puts("\nBootstrap mode: ");
    uart_puts(g_slab_lookup->bootstrap_mode ? "yes" : "no");
    uart_puts("\n\n");
    
    // Calculate average chain length
    if (g_slab_lookup->num_entries > 0) {
        uint32_t non_empty_buckets = 0;
        uint32_t max_chain_length = 0;
        
        for (size_t i = 0; i < g_slab_lookup->num_buckets; i++) {
            if (g_slab_lookup->buckets[i].head) {
                non_empty_buckets++;
                
                // Count chain length
                uint32_t chain_len = 0;
                struct slab_hash_entry *entry = g_slab_lookup->buckets[i].head;
                while (entry) {
                    chain_len++;
                    entry = entry->next;
                }
                
                if (chain_len > max_chain_length) {
                    max_chain_length = chain_len;
                }
            }
        }
        
        uart_puts("Non-empty buckets: ");
        uart_putdec(non_empty_buckets);
        uart_puts("\nMax chain length: ");
        uart_putdec(max_chain_length);
        uart_puts("\n");
    }
}