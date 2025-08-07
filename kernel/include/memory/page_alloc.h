/*
 * kernel/include/memory/page_alloc.h
 *
 * Page-level allocator using buddy system
 * Handles allocations between slab allocator and PMM
 */

#ifndef PAGE_ALLOC_H
#define PAGE_ALLOC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define PAGE_ALLOC_MAX_ORDER 12

// Chunk management constants - no magic numbers
#define PAGE_ALLOC_MIN_CHUNK_SIZE (2 * 1024 * 1024)   // Minimum chunk: 2MB
#define PAGE_ALLOC_MAX_CHUNK_SIZE (16 * 1024 * 1024)  // Maximum chunk: 16MB
#define PAGE_ALLOC_INITIAL_CHUNK_SIZE (2 * 1024 * 1024) // Initial pre-allocation

// Thresholds for chunk management
#define PAGE_ALLOC_MIN_CHUNKS_TO_KEEP 2  // Always keep at least 2 chunks
#define PAGE_ALLOC_CHUNK_CLEANUP_THRESHOLD 3  // Start cleanup when > 3 chunks
#define PAGE_ALLOC_CLEANUP_MIN_ORDER 9   // Only cleanup after coalescing to order 9+
#define PAGE_ALLOC_MAX_BLOCKS_PER_CHUNK 256  // Max tracking blocks per chunk

// Allocation strategy thresholds  
#define PAGE_ALLOC_LARGE_ORDER_THRESHOLD 10  // Orders >= 10 are "large"
#define PAGE_ALLOC_MEDIUM_ORDER_THRESHOLD 7  // Orders 7-9 are "medium"
#define PAGE_ALLOC_MEDIUM_CHUNK_SIZE (4 * 1024 * 1024)  // Use 4MB chunks for medium

#define PAGE_ALLOC_MAGIC 0xDEADBEEF  // Magic for corruption detection

struct page_block {
    struct page_block *next;
    struct page_block *prev;
    uint64_t phys_addr;      
    uint32_t order;          
    uint32_t flags;          
};

#define BLOCK_FLAG_ALLOCATED  0x01
#define BLOCK_FLAG_BUDDY_LEFT 0x02   

struct page_free_list {
    struct page_block *head;
    uint32_t count;          
};

struct page_chunk {
    struct page_chunk *next;
    uint64_t phys_addr;      
    size_t size;             
    struct page_block *blocks; 
    uint32_t num_blocks;     
    uint32_t free_blocks;    
    uint32_t allocated_blocks;  // Track how many blocks are allocated
};

struct page_allocator {
    struct page_free_list free_lists[PAGE_ALLOC_MAX_ORDER + 1];
    struct page_chunk *chunks;       
    uint32_t total_chunks;
    uint32_t total_pages;
    uint32_t free_pages;
    bool initialized;
};

struct page_alloc_stats {
    uint64_t allocations[PAGE_ALLOC_MAX_ORDER + 1];
    uint64_t frees[PAGE_ALLOC_MAX_ORDER + 1];
    uint64_t splits[PAGE_ALLOC_MAX_ORDER + 1];
    uint64_t coalesces[PAGE_ALLOC_MAX_ORDER + 1];
    uint64_t current_allocated[PAGE_ALLOC_MAX_ORDER + 1];
    uint64_t pmm_chunks_allocated;
    uint64_t pmm_chunks_freed;
};

void page_alloc_init(void);

uint64_t page_alloc(uint32_t order);

void page_free(uint64_t phys_addr, uint32_t order);

uint64_t page_alloc_multiple(size_t num_pages);

void page_free_multiple(uint64_t phys_addr, size_t num_pages);

uint32_t page_get_order_for_size(size_t size);

size_t page_get_size_for_order(uint32_t order);

void page_alloc_get_stats(struct page_alloc_stats *stats);

void page_alloc_print_stats(void);

#ifdef DEBUG_PAGE_ALLOC
void page_alloc_dump_free_lists(void);
void page_alloc_check_integrity(void);
#endif

uint64_t page_alloc_chunk_from_pmm(size_t size);
void page_alloc_return_chunk_to_pmm(struct page_chunk *chunk);
void page_alloc_check_empty_chunks(void);

struct page_block *page_alloc_find_buddy(struct page_block *block);
void page_alloc_split_block(struct page_block *block, uint32_t target_order);
struct page_block *page_alloc_coalesce_blocks(struct page_block *block1, struct page_block *block2);

void page_alloc_add_to_free_list(struct page_block *block, uint32_t order);
void page_alloc_remove_from_free_list(struct page_block *block, uint32_t order);

bool page_alloc_is_initialized(void);

#endif 