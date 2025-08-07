/*
 * kernel/memory/page_alloc.c
 *
 * Page-level allocator implementation using buddy system
 * 
 * THREAD SAFETY:
 * This allocator is NOT thread-safe. Callers must provide external
 * synchronization (e.g., spinlocks) when used in multi-threaded contexts.
 * 
 * INVARIANTS:
 * - All blocks are aligned to PAGE_SIZE
 * - Buddy blocks can only be coalesced if they have the same order
 * - Each chunk maintains a list of blocks, some allocated, some free
 * - Empty chunks are automatically returned to PMM to reduce memory waste
 */

#include <memory/page_alloc.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <uart.h>
#include <string.h>
#include <stdbool.h>

static struct page_allocator g_page_alloc = {0};
static struct page_alloc_stats g_stats = {0};

#define PAGE_ALLOC_DEBUG 0

#if PAGE_ALLOC_DEBUG
static void page_debug(const char *msg) {
    uart_puts("[PAGE_ALLOC] ");
    uart_puts(msg);
}

static void page_debug_hex(const char *msg, uint64_t val) {
    uart_puts("[PAGE_ALLOC] ");
    uart_puts(msg);
    uart_puts(": 0x");
    uart_puthex(val);
    uart_puts("\n");
}

static void page_debug_dec(const char *msg, uint64_t val) {
    uart_puts("[PAGE_ALLOC] ");
    uart_puts(msg);
    uart_puts(": ");
    uart_putdec(val);
    uart_puts("\n");
}
#else
#define page_debug(msg) ((void)0)
#define page_debug_hex(msg, val) ((void)0)
#define page_debug_dec(msg, val) ((void)0)
#endif

static inline uint32_t ilog2(size_t n) {
    uint32_t log = 0;
    while ((1UL << log) < n) {
        log++;
    }
    return log;
}

static inline bool is_power_of_two(size_t n) {
    return n && !(n & (n - 1));
}

static inline uint64_t align_down(uint64_t addr, size_t align) {
    return addr & ~(align - 1);
}

static inline uint64_t align_up(uint64_t addr, size_t align) {
    return (addr + align - 1) & ~(align - 1);
}

// Validate block integrity
static bool validate_block(struct page_block *block) {
    if (!block) return false;
    
    // Check for obviously corrupted values
    if (block->order > PAGE_ALLOC_MAX_ORDER) {
        uart_puts("CORRUPTION: Invalid block order: ");
        uart_putdec(block->order);
        uart_puts("\n");
        return false;
    }
    
    // Check alignment
    if (block->phys_addr != 0 && (block->phys_addr & (PAGE_SIZE - 1)) != 0) {
        uart_puts("CORRUPTION: Misaligned block address\n");
        return false;
    }
    
    return true;
}

// Validate chunk integrity  
static bool validate_chunk(struct page_chunk *chunk) {
    if (!chunk) return false;
    
    // Check size is reasonable
    if (chunk->size == 0 || chunk->size > PAGE_ALLOC_MAX_CHUNK_SIZE) {
        uart_puts("CORRUPTION: Invalid chunk size\n");
        return false;
    }
    
    // Check block count
    if (chunk->num_blocks > PAGE_ALLOC_MAX_BLOCKS_PER_CHUNK) {
        uart_puts("CORRUPTION: Too many blocks in chunk\n");
        return false;
    }
    
    return true;
}

void page_alloc_init(void) {
    if (g_page_alloc.initialized) {
        uart_puts("page_alloc: already initialized\n");
        return;
    }

    for (int i = 0; i <= PAGE_ALLOC_MAX_ORDER; i++) {
        g_page_alloc.free_lists[i].head = NULL;
        g_page_alloc.free_lists[i].count = 0;
    }

    g_page_alloc.chunks = NULL;
    g_page_alloc.total_chunks = 0;
    g_page_alloc.total_pages = 0;
    g_page_alloc.free_pages = 0;
    g_page_alloc.initialized = true;

    page_debug("Page allocator initialized with max order\n");
    
    // Pre-allocate initial chunk for better performance
    if (pmm_is_initialized()) {
        uint64_t chunk = page_alloc_chunk_from_pmm(PAGE_ALLOC_INITIAL_CHUNK_SIZE);
        if (chunk != 0) {
            page_debug("Pre-allocated initial chunk\n");
        }
    }
}

uint32_t page_get_order_for_size(size_t size) {
    if (size == 0) return 0;
    
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    if (pages == 1) return 0;
    
    uint32_t order = ilog2(pages);
    
    if ((1UL << order) < pages) {
        order++;
    }
    
    if (order > PAGE_ALLOC_MAX_ORDER) {
        return PAGE_ALLOC_MAX_ORDER + 1; 
    }
    
    return order;
}

size_t page_get_size_for_order(uint32_t order) {
    if (order > PAGE_ALLOC_MAX_ORDER) {
        return 0;
    }
    return (1UL << order) * PAGE_SIZE;
}

static uint64_t get_buddy_addr(uint64_t addr, uint32_t order) {
    return addr ^ (1UL << (order + PAGE_SHIFT));
}

struct page_block *page_alloc_find_buddy(struct page_block *block) {
    if (!block || block->order > PAGE_ALLOC_MAX_ORDER) {
        return NULL;
    }
    
    uint64_t buddy_addr = get_buddy_addr(block->phys_addr, block->order);
    
    struct page_chunk *chunk = g_page_alloc.chunks;
    while (chunk) {
        if (buddy_addr >= chunk->phys_addr && 
            buddy_addr < chunk->phys_addr + chunk->size) {
            
            for (uint32_t i = 0; i < chunk->num_blocks; i++) {
                if (chunk->blocks[i].phys_addr == buddy_addr &&
                    chunk->blocks[i].order == block->order &&
                    !(chunk->blocks[i].flags & BLOCK_FLAG_ALLOCATED)) {
                    return &chunk->blocks[i];
                }
            }
            break;
        }
        chunk = chunk->next;
    }
    
    return NULL;
}

void page_alloc_add_to_free_list(struct page_block *block, uint32_t order) {
    if (!block || order > PAGE_ALLOC_MAX_ORDER) {
        return;
    }
    
    struct page_free_list *list = &g_page_alloc.free_lists[order];
    
    block->next = list->head;
    block->prev = NULL;
    if (list->head) {
        list->head->prev = block;
    }
    list->head = block;
    list->count++;
    
    block->order = order;
    block->flags &= ~BLOCK_FLAG_ALLOCATED;
    
    page_debug_hex("Added block to free list", block->phys_addr);
}

void page_alloc_remove_from_free_list(struct page_block *block, uint32_t order) {
    if (!block || order > PAGE_ALLOC_MAX_ORDER) {
        return;
    }
    
    struct page_free_list *list = &g_page_alloc.free_lists[order];
    
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        list->head = block->next;
    }
    
    if (block->next) {
        block->next->prev = block->prev;
    }
    
    list->count--;
    block->next = NULL;
    block->prev = NULL;
    
    page_debug_hex("Removed block from free list", block->phys_addr);
}

void page_alloc_split_block(struct page_block *block, uint32_t target_order) {
    if (!block || block->order <= target_order || block->order > PAGE_ALLOC_MAX_ORDER) {
        return;
    }
    
    page_debug_hex("Splitting block", block->phys_addr);
    
    while (block->order > target_order) {
        uint32_t current_order = block->order;
        block->order--;
        
        size_t half_size = 1UL << (block->order + PAGE_SHIFT);
        uint64_t buddy_addr = block->phys_addr + half_size;
        
        struct page_chunk *chunk = g_page_alloc.chunks;
        struct page_block *buddy = NULL;
        
        while (chunk) {
            if (buddy_addr >= chunk->phys_addr && 
                buddy_addr < chunk->phys_addr + chunk->size) {
                
                for (uint32_t i = 0; i < chunk->num_blocks; i++) {
                    if (chunk->blocks[i].flags & BLOCK_FLAG_ALLOCATED) {
                        continue;
                    }
                    if (chunk->blocks[i].phys_addr == 0) {
                        buddy = &chunk->blocks[i];
                        buddy->phys_addr = buddy_addr;
                        buddy->order = block->order;
                        buddy->flags = 0;
                        buddy->next = NULL;
                        buddy->prev = NULL;
                        break;
                    }
                }
                break;
            }
            chunk = chunk->next;
        }
        
        if (buddy) {
            page_alloc_add_to_free_list(buddy, buddy->order);
            g_stats.splits[current_order]++;
        }
    }
}

struct page_block *page_alloc_coalesce_blocks(struct page_block *block1, struct page_block *block2) {
    if (!block1 || !block2 || block1->order != block2->order) {
        return NULL;
    }
    
    if (block1->order >= PAGE_ALLOC_MAX_ORDER) {
        return NULL;
    }
    
    struct page_block *left = (block1->phys_addr < block2->phys_addr) ? block1 : block2;
    struct page_block *right = (block1->phys_addr < block2->phys_addr) ? block2 : block1;
    
    if (get_buddy_addr(left->phys_addr, left->order) != right->phys_addr) {
        return NULL;
    }
    
    page_debug_hex("Coalescing blocks", left->phys_addr);
    
    left->order++;
    right->phys_addr = 0;
    right->order = 0;
    right->flags = 0;
    
    g_stats.coalesces[left->order - 1]++;
    
    return left;
}

uint64_t page_alloc_chunk_from_pmm(size_t size) {
    if (!pmm_is_initialized()) {
        uart_puts("page_alloc: PMM not initialized\n");
        return 0;
    }
    
    size = align_up(size, PAGE_SIZE);
    size_t pages = size / PAGE_SIZE;
    
    uint64_t phys_addr = pmm_alloc_pages(pages);
    if (phys_addr == 0) {
        return 0;  // Don't print error - caller will handle it
    }
    
    void *chunk_mem = (void *)PHYS_TO_DMAP(phys_addr);
    struct page_chunk *chunk = (struct page_chunk *)chunk_mem;
    
    // Reserve first page for chunk metadata and block array
    chunk->phys_addr = phys_addr + PAGE_SIZE;
    chunk->size = size - PAGE_SIZE;
    chunk->next = g_page_alloc.chunks;
    
    // Limit blocks array to fit in first page with chunk header
    size_t max_blocks = (PAGE_SIZE - sizeof(struct page_chunk)) / sizeof(struct page_block);
    if (max_blocks > PAGE_ALLOC_MAX_BLOCKS_PER_CHUNK) {
        max_blocks = PAGE_ALLOC_MAX_BLOCKS_PER_CHUNK;
    }
    
    chunk->blocks = (struct page_block *)((uint8_t *)chunk + sizeof(struct page_chunk));
    chunk->num_blocks = max_blocks;
    chunk->free_blocks = max_blocks;
    chunk->allocated_blocks = 0;  // Initially no blocks are allocated
    
    memset(chunk->blocks, 0, max_blocks * sizeof(struct page_block));
    
    // All memory after first page is usable
    size_t usable_size = chunk->size;
    uint64_t usable_addr = chunk->phys_addr;
    
    // Create multiple blocks to better utilize the chunk space
    int block_idx = 0;
    uint64_t current_addr = usable_addr;
    size_t remaining_size = usable_size;
    
    // Split the chunk into reasonably sized blocks
    while (remaining_size > 0 && block_idx < (int)chunk->num_blocks) {
        // Find the largest order that fits
        // Cap at order 11 to avoid alignment issues with order 12
        // Order 12 allocations should go directly through PMM
        uint32_t order = (PAGE_ALLOC_MAX_ORDER > 0) ? PAGE_ALLOC_MAX_ORDER - 1 : 0;
        bool block_created = false;
        
        // Try orders from 11 down to 0
        do {
            size_t block_size = 1UL << (order + PAGE_SHIFT);
            uint64_t aligned_addr = align_up(current_addr, block_size);
            
            // Check if this order fits with alignment
            if (aligned_addr >= current_addr &&
                aligned_addr + block_size <= usable_addr + usable_size &&
                block_size <= remaining_size) {
                // Create this block
                chunk->blocks[block_idx].phys_addr = aligned_addr;
                chunk->blocks[block_idx].order = order;
                chunk->blocks[block_idx].flags = 0;
                chunk->blocks[block_idx].next = NULL;
                chunk->blocks[block_idx].prev = NULL;
                
                // Add to free list
                page_alloc_add_to_free_list(&chunk->blocks[block_idx], order);
                
                g_page_alloc.total_pages += (1UL << order);
                g_page_alloc.free_pages += (1UL << order);
                
                // Move to next position
                current_addr = aligned_addr + block_size;
                remaining_size = (usable_addr + usable_size > current_addr) ? 
                                 (usable_addr + usable_size - current_addr) : 0;
                block_idx++;
                block_created = true;
                break;
            }
            
            if (order == 0) break;  // Prevent underflow
            order--;
        } while (order <= PAGE_ALLOC_MAX_ORDER);  // This condition ensures we try order 0
        
        // If we couldn't create any block, we're done
        if (!block_created) {
            break;  // Can't use remaining space
        }
    }
    
    g_page_alloc.chunks = chunk;
    g_page_alloc.total_chunks++;
    g_stats.pmm_chunks_allocated++;
    
    page_debug_hex("Allocated chunk from PMM", phys_addr);
    
    return phys_addr;
}

uint64_t page_alloc(uint32_t order) {
    if (!g_page_alloc.initialized) {
        uart_puts("page_alloc: not initialized\n");
        return 0;
    }
    
    // For order-12 and above, go directly to PMM to avoid alignment issues
    if (order >= PAGE_ALLOC_MAX_ORDER) {
        size_t pages = 1UL << order;
        uint64_t phys_addr = pmm_alloc_pages(pages);
        if (phys_addr) {
            g_stats.allocations[PAGE_ALLOC_MAX_ORDER]++;  // Track as max order
            g_stats.current_allocated[PAGE_ALLOC_MAX_ORDER]++;
        }
        return phys_addr;
    }
    
    // Try to find a free block of the requested order or larger
    // We only create blocks up to order 11 in chunks
    uint32_t max_chunk_order = (PAGE_ALLOC_MAX_ORDER > 0) ? PAGE_ALLOC_MAX_ORDER - 1 : 0;
    for (uint32_t current_order = order; current_order <= max_chunk_order; current_order++) {
        struct page_free_list *list = &g_page_alloc.free_lists[current_order];
        
        if (list->head) {
            struct page_block *block = list->head;
            page_alloc_remove_from_free_list(block, current_order);
            
            if (current_order > order) {
                page_alloc_split_block(block, order);
            }
            
            block->flags |= BLOCK_FLAG_ALLOCATED;
            
            // Track allocation in chunk
            struct page_chunk *chunk = g_page_alloc.chunks;
            while (chunk) {
                if (block >= chunk->blocks && block < chunk->blocks + chunk->num_blocks) {
                    chunk->allocated_blocks++;
                    break;
                }
                chunk = chunk->next;
            }
            
            size_t pages = 1UL << order;
            g_page_alloc.free_pages -= pages;
            g_stats.allocations[order]++;
            g_stats.current_allocated[order]++;
            
            page_debug_hex("Allocated block", block->phys_addr);
            return block->phys_addr;
        }
    }
    
    // No free blocks available, need to allocate a new chunk from PMM
    // Use smarter sizing strategy based on allocation pattern
    size_t needed_size = 1UL << (order + PAGE_SHIFT);
    size_t chunk_size;
    
    if (order >= PAGE_ALLOC_LARGE_ORDER_THRESHOLD) {
        // For very large allocations, allocate exactly what's needed plus overhead
        chunk_size = align_up(needed_size + PAGE_SIZE, PAGE_SIZE);
    } else if (order >= PAGE_ALLOC_MEDIUM_ORDER_THRESHOLD) {
        // For medium allocations, use medium-sized chunks
        chunk_size = PAGE_ALLOC_MEDIUM_CHUNK_SIZE;
    } else {
        // For small allocations, use minimum chunk size
        chunk_size = PAGE_ALLOC_MIN_CHUNK_SIZE;
    }
    
    // Ensure chunk is large enough for the requested allocation
    if (chunk_size < needed_size + PAGE_SIZE) {
        chunk_size = align_up(needed_size + PAGE_SIZE, PAGE_SIZE);
    }
    
    // Try to allocate the chunk
    uint64_t chunk_addr = page_alloc_chunk_from_pmm(chunk_size);
    if (chunk_addr == 0 && chunk_size > PAGE_ALLOC_MIN_CHUNK_SIZE) {
        // If allocation failed, try minimum size
        chunk_size = PAGE_ALLOC_MIN_CHUNK_SIZE;
        if (chunk_size >= needed_size + PAGE_SIZE) {
            chunk_addr = page_alloc_chunk_from_pmm(chunk_size);
        }
    }
    
    if (chunk_addr == 0) {
        return 0;
    }
    
    // Recursively try allocation now that we have a new chunk
    return page_alloc(order);
}

void page_free(uint64_t phys_addr, uint32_t order) {
    if (!g_page_alloc.initialized) {
        uart_puts("page_free: not initialized\n");
        return;
    }
    
    // Validate input
    if (phys_addr == 0) {
        uart_puts("page_free: NULL address\n");
        return;
    }
    
    if ((phys_addr & (PAGE_SIZE - 1)) != 0) {
        uart_puts("page_free: Misaligned address\n");
        return;
    }
    
    // For order-12 and above, these went directly to PMM
    if (order >= PAGE_ALLOC_MAX_ORDER) {
        size_t pages = 1UL << order;
        pmm_free_pages(phys_addr, pages);
        g_stats.frees[PAGE_ALLOC_MAX_ORDER]++;  // Track as max order
        g_stats.current_allocated[PAGE_ALLOC_MAX_ORDER]--;
        return;
    }
    
    struct page_chunk *chunk = g_page_alloc.chunks;
    struct page_chunk *owning_chunk = NULL;
    struct page_block *block = NULL;
    
    // Find the chunk and block for this address
    while (chunk) {
        if (phys_addr >= chunk->phys_addr && 
            phys_addr < chunk->phys_addr + chunk->size) {
            owning_chunk = chunk;
            
            // Look for existing block
            for (uint32_t i = 0; i < chunk->num_blocks; i++) {
                if (chunk->blocks[i].phys_addr == phys_addr) {
                    block = &chunk->blocks[i];
                    break;
                }
            }
            
            // If not found, find a free slot to track this block
            if (!block) {
                for (uint32_t i = 0; i < chunk->num_blocks; i++) {
                    if (chunk->blocks[i].phys_addr == 0) {
                        block = &chunk->blocks[i];
                        block->phys_addr = phys_addr;
                        block->order = order;
                        block->flags = BLOCK_FLAG_ALLOCATED;
                        // This block was allocated but not tracked, so increment counter
                        owning_chunk->allocated_blocks++;
                        break;
                    }
                }
            }
            break;
        }
        chunk = chunk->next;
    }
    
    if (!block || !owning_chunk) {
        uart_puts("page_free: Block not found\n");
        return;
    }
    
    if (!(block->flags & BLOCK_FLAG_ALLOCATED)) {
        uart_puts("page_free: Double free detected\n");
        return;
    }
    
    block->flags &= ~BLOCK_FLAG_ALLOCATED;
    block->order = order;
    
    // Track deallocation in chunk
    owning_chunk->allocated_blocks--;
    
    size_t pages = 1UL << order;
    g_page_alloc.free_pages += pages;
    g_stats.frees[order]++;
    g_stats.current_allocated[order]--;
    
    // Try to coalesce with buddies
    while (order < PAGE_ALLOC_MAX_ORDER) {
        uint64_t buddy_addr = get_buddy_addr(block->phys_addr, order);
        struct page_block *buddy = NULL;
        
        // Look for buddy in the same chunk (more efficient)
        if (buddy_addr >= owning_chunk->phys_addr && 
            buddy_addr < owning_chunk->phys_addr + owning_chunk->size) {
            for (uint32_t i = 0; i < owning_chunk->num_blocks; i++) {
                if (owning_chunk->blocks[i].phys_addr == buddy_addr &&
                    owning_chunk->blocks[i].order == order &&
                    !(owning_chunk->blocks[i].flags & BLOCK_FLAG_ALLOCATED)) {
                    buddy = &owning_chunk->blocks[i];
                    break;
                }
            }
        }
        
        if (!buddy) {
            break;  // No buddy found, can't coalesce
        }
        
        page_alloc_remove_from_free_list(buddy, order);
        
        block = page_alloc_coalesce_blocks(block, buddy);
        if (!block) {
            break;
        }
        order = block->order;
    }
    
    page_alloc_add_to_free_list(block, block->order);
    
    page_debug_hex("Freed block", phys_addr);
    
    // If this chunk is now empty, check all chunks for cleanup
    // We can't free the current chunk while using it, so we check all chunks
    if (owning_chunk->allocated_blocks == 0 && 
        g_page_alloc.total_chunks > PAGE_ALLOC_MIN_CHUNKS_TO_KEEP) {
        page_alloc_check_empty_chunks();
    }
}

void page_alloc_return_chunk_to_pmm(struct page_chunk *chunk) {
    if (!chunk) return;
    
    // Calculate the original allocation size (chunk size + overhead page)
    size_t total_size = chunk->size + PAGE_SIZE;
    size_t pages = total_size / PAGE_SIZE;
    
    // Get physical address (chunk is at the start of allocation)
    uint64_t phys_addr = DMAP_TO_PHYS((uint64_t)chunk);
    
    // Remove chunk from the list
    struct page_chunk **pp = &g_page_alloc.chunks;
    while (*pp) {
        if (*pp == chunk) {
            *pp = chunk->next;
            break;
        }
        pp = &(*pp)->next;
    }
    
    // Update statistics
    g_page_alloc.total_chunks--;
    g_stats.pmm_chunks_freed++;
    
    // Return pages to PMM
    pmm_free_pages(phys_addr, pages);
    
    page_debug_hex("Returned chunk to PMM", phys_addr);
}

void page_alloc_check_empty_chunks(void) {
    // Keep minimum chunks for performance
    if (g_page_alloc.total_chunks <= PAGE_ALLOC_MIN_CHUNKS_TO_KEEP) {
        return;
    }
    
    struct page_chunk *chunk = g_page_alloc.chunks;
    struct page_chunk *prev = NULL;
    
    while (chunk) {
        struct page_chunk *next = chunk->next;
        
        // Use the allocated_blocks counter for efficiency
        if (chunk->allocated_blocks == 0 && 
            g_page_alloc.total_chunks > PAGE_ALLOC_MIN_CHUNKS_TO_KEEP) {
            // First remove all blocks from free lists
            for (uint32_t i = 0; i < chunk->num_blocks; i++) {
                if (chunk->blocks[i].phys_addr != 0 && 
                    !(chunk->blocks[i].flags & BLOCK_FLAG_ALLOCATED)) {
                    page_alloc_remove_from_free_list(&chunk->blocks[i], chunk->blocks[i].order);
                    g_page_alloc.total_pages -= (1UL << chunk->blocks[i].order);
                    g_page_alloc.free_pages -= (1UL << chunk->blocks[i].order);
                }
            }
            
            // Fix the linked list BEFORE calling return_chunk_to_pmm
            if (prev) {
                prev->next = next;
            } else {
                g_page_alloc.chunks = next;
            }
            
            // Update statistics
            g_page_alloc.total_chunks--;
            g_stats.pmm_chunks_freed++;
            
            // Calculate size and return to PMM
            size_t total_size = chunk->size + PAGE_SIZE;
            size_t pages = total_size / PAGE_SIZE;
            uint64_t phys_addr = DMAP_TO_PHYS((uint64_t)chunk);
            
            pmm_free_pages(phys_addr, pages);
            page_debug_hex("Returned chunk to PMM", phys_addr);
            
            // Don't update prev since we removed this chunk
        } else {
            prev = chunk;
        }
        
        chunk = next;
    }
}

uint64_t page_alloc_multiple(size_t num_pages) {
    if (num_pages == 0) {
        return 0;
    }
    
    uint32_t order = page_get_order_for_size(num_pages * PAGE_SIZE);
    if (order > PAGE_ALLOC_MAX_ORDER) {
        size_t pages_allocated = 0;
        uint64_t first_addr = 0;
        
        while (pages_allocated < num_pages) {
            size_t remaining = num_pages - pages_allocated;
            uint32_t chunk_order = PAGE_ALLOC_MAX_ORDER;
            
            while (chunk_order > 0 && (1UL << chunk_order) > remaining) {
                chunk_order--;
            }
            
            uint64_t addr = page_alloc(chunk_order);
            if (addr == 0) {
                if (first_addr != 0) {
                    page_free_multiple(first_addr, pages_allocated);
                }
                return 0;
            }
            
            if (first_addr == 0) {
                first_addr = addr;
            }
            
            pages_allocated += (1UL << chunk_order);
        }
        
        return first_addr;
    }
    
    return page_alloc(order);
}

void page_free_multiple(uint64_t phys_addr, size_t num_pages) {
    if (num_pages == 0) {
        return;
    }
    
    uint32_t order = page_get_order_for_size(num_pages * PAGE_SIZE);
    if (order > PAGE_ALLOC_MAX_ORDER) {
        uint64_t current_addr = phys_addr;
        size_t pages_freed = 0;
        
        while (pages_freed < num_pages) {
            size_t remaining = num_pages - pages_freed;
            uint32_t chunk_order = PAGE_ALLOC_MAX_ORDER;
            
            while (chunk_order > 0 && (1UL << chunk_order) > remaining) {
                chunk_order--;
            }
            
            page_free(current_addr, chunk_order);
            
            current_addr += (1UL << chunk_order) * PAGE_SIZE;
            pages_freed += (1UL << chunk_order);
        }
        
        return;
    }
    
    page_free(phys_addr, order);
}

void page_alloc_get_stats(struct page_alloc_stats *stats) {
    if (stats) {
        memcpy(stats, &g_stats, sizeof(struct page_alloc_stats));
    }
}

void page_alloc_print_stats(void) {
    uart_puts("\nPage Allocator Statistics:\n");
    uart_puts("========================\n");
    uart_puts("Total chunks: ");
    uart_putdec(g_page_alloc.total_chunks);
    uart_puts("\nTotal pages: ");
    uart_putdec(g_page_alloc.total_pages);
    uart_puts("\nFree pages: ");
    uart_putdec(g_page_alloc.free_pages);
    uart_puts("\nUsed pages: ");
    uart_putdec(g_page_alloc.total_pages - g_page_alloc.free_pages);
    uart_puts("\n\nPer-order statistics:\n");
    uart_puts("Order | Pages  | Allocs | Frees | Current | Free\n");
    uart_puts("------|--------|--------|-------|---------|-----\n");
    
    for (int i = 0; i <= PAGE_ALLOC_MAX_ORDER; i++) {
        uart_puts("  ");
        uart_putdec(i);
        uart_puts("   | ");
        uart_putdec(1UL << i);
        uart_puts("     | ");
        uart_putdec(g_stats.allocations[i]);
        uart_puts("      | ");
        uart_putdec(g_stats.frees[i]);
        uart_puts("     | ");
        uart_putdec(g_stats.current_allocated[i]);
        uart_puts("       | ");
        uart_putdec(g_page_alloc.free_lists[i].count);
        uart_puts("\n");
    }
    
    uart_puts("\nPMM chunks allocated: ");
    uart_putdec(g_stats.pmm_chunks_allocated);
    uart_puts("\nPMM chunks freed: ");
    uart_putdec(g_stats.pmm_chunks_freed);
    uart_puts("\n");
}

bool page_alloc_is_initialized(void) {
    return g_page_alloc.initialized;
}

#ifdef DEBUG_PAGE_ALLOC
void page_alloc_dump_free_lists(void) {
    uart_puts("\nFree Lists:\n");
    for (int i = 0; i <= PAGE_ALLOC_MAX_ORDER; i++) {
        struct page_free_list *list = &g_page_alloc.free_lists[i];
        if (list->count > 0) {
            uart_puts("Order ");
            uart_putdec(i);
            uart_puts(" (");
            uart_putdec((1UL << i) * 4);
            uart_puts(" KB): ");
            uart_putdec(list->count);
            uart_puts(" blocks\n");
            
            struct page_block *block = list->head;
            int count = 0;
            while (block && count < 5) {
                uart_puts("  [");
                uart_putdec(count);
                uart_puts("] addr=0x");
                uart_puthex(block->phys_addr);
                uart_puts("\n");
                block = block->next;
                count++;
            }
            if (list->count > 5) {
                uart_puts("  ... and ");
                uart_putdec(list->count - 5);
                uart_puts(" more\n");
            }
        }
    }
}

void page_alloc_check_integrity(void) {
    uart_puts("\nChecking page allocator integrity...\n");
    
    for (int i = 0; i <= PAGE_ALLOC_MAX_ORDER; i++) {
        struct page_free_list *list = &g_page_alloc.free_lists[i];
        struct page_block *block = list->head;
        uint32_t count = 0;
        
        while (block) {
            if (block->order != i) {
                uart_puts("ERROR: Block in order ");
                uart_putdec(i);
                uart_puts(" list has order ");
                uart_putdec(block->order);
                uart_puts("\n");
            }
            if (block->flags & BLOCK_FLAG_ALLOCATED) {
                uart_puts("ERROR: Allocated block in free list (order ");
                uart_putdec(i);
                uart_puts(")\n");
            }
            count++;
            block = block->next;
        }
        
        if (count != list->count) {
            uart_puts("ERROR: Order ");
            uart_putdec(i);
            uart_puts(" list count mismatch: counted ");
            uart_putdec(count);
            uart_puts(", stored ");
            uart_putdec(list->count);
            uart_puts("\n");
        }
    }
    
    uart_puts("Integrity check complete.\n");
}
#endif 