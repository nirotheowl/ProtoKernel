#ifndef _IRQ_ALLOC_H
#define _IRQ_ALLOC_H

#include <stdint.h>
#include <stdbool.h>
#include <spinlock.h>

// Virtual IRQ allocator
struct virq_allocator {
    uint32_t next_virq;
    uint32_t max_virq;
    unsigned long *bitmap;      // Allocation bitmap
    spinlock_t lock;
};

// Allocation functions
uint32_t virq_alloc(void);
uint32_t virq_alloc_range(uint32_t count);
void virq_free(uint32_t virq);
void virq_free_range(uint32_t virq, uint32_t count);

// Initialize the virtual IRQ allocator
void virq_allocator_init(void);

// Check if a virq is allocated
bool virq_is_allocated(uint32_t virq);

// Get allocator statistics
uint32_t virq_get_allocated_count(void);
uint32_t virq_get_max_allocated(void);

#endif /* _IRQ_ALLOC_H */