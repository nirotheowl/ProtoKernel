#include <irq/irq_alloc.h>
#include <irq/irq.h>
#include <memory/kmalloc.h>
#include <memory/slab.h>
#include <string.h>
#include <panic.h>

#define MAX_VIRQ        1024
#define BITS_PER_LONG   64
#define BITMAP_SIZE     ((MAX_VIRQ + BITS_PER_LONG - 1) / BITS_PER_LONG)

static struct virq_allocator virq_alloc_data = {
    .next_virq = 1,     // Start from 1, 0 is reserved as invalid
    .max_virq = MAX_VIRQ,
    .bitmap = NULL,
    .lock = SPINLOCK_INITIALIZER
};

// Helper functions for bitmap operations
static inline void bitmap_set_bit(unsigned long *bitmap, uint32_t bit) {
    bitmap[bit / BITS_PER_LONG] |= (1UL << (bit % BITS_PER_LONG));
}

static inline void bitmap_clear_bit(unsigned long *bitmap, uint32_t bit) {
    bitmap[bit / BITS_PER_LONG] &= ~(1UL << (bit % BITS_PER_LONG));
}

static inline bool bitmap_test_bit(unsigned long *bitmap, uint32_t bit) {
    return (bitmap[bit / BITS_PER_LONG] & (1UL << (bit % BITS_PER_LONG))) != 0;
}

// Find first clear bit in bitmap
static uint32_t bitmap_find_first_zero(unsigned long *bitmap, uint32_t max_bits) {
    for (uint32_t i = 0; i < max_bits; i++) {
        if (!bitmap_test_bit(bitmap, i)) {
            return i;
        }
    }
    return max_bits;  // No free bit found
}

// Find range of consecutive clear bits
static uint32_t bitmap_find_zero_range(unsigned long *bitmap, uint32_t max_bits, uint32_t count) {
    uint32_t start = 0;
    uint32_t consecutive = 0;
    
    for (uint32_t i = 0; i < max_bits; i++) {
        if (!bitmap_test_bit(bitmap, i)) {
            if (consecutive == 0) {
                start = i;
            }
            consecutive++;
            if (consecutive == count) {
                return start;
            }
        } else {
            consecutive = 0;
        }
    }
    
    return max_bits;  // No suitable range found
}

void virq_allocator_init(void) {
    unsigned long flags;
    
    spin_lock_irqsave(&virq_alloc_data.lock, flags);
    
    if (virq_alloc_data.bitmap == NULL) {
        virq_alloc_data.bitmap = kmalloc(BITMAP_SIZE * sizeof(unsigned long), KM_ZERO);
        if (!virq_alloc_data.bitmap) {
            panic("Failed to allocate virtual IRQ bitmap");
        }
        
        // Already zeroed by KM_ZERO flag
        
        // Reserve IRQ 0 as invalid
        bitmap_set_bit(virq_alloc_data.bitmap, 0);
    }
    
    spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
}

uint32_t virq_alloc(void) {
    unsigned long flags;
    uint32_t virq;
    
    if (!virq_alloc_data.bitmap) {
        virq_allocator_init();
    }
    
    spin_lock_irqsave(&virq_alloc_data.lock, flags);
    
    // Find first available virq
    virq = bitmap_find_first_zero(virq_alloc_data.bitmap, virq_alloc_data.max_virq);
    
    if (virq >= virq_alloc_data.max_virq) {
        spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
        return IRQ_INVALID;
    }
    
    // Mark as allocated
    bitmap_set_bit(virq_alloc_data.bitmap, virq);
    
    // Update next_virq hint for faster allocation
    if (virq >= virq_alloc_data.next_virq) {
        virq_alloc_data.next_virq = virq + 1;
    }
    
    spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
    
    return virq;
}

uint32_t virq_alloc_range(uint32_t count) {
    unsigned long flags;
    uint32_t virq;
    
    if (count == 0 || count > virq_alloc_data.max_virq) {
        return IRQ_INVALID;
    }
    
    if (!virq_alloc_data.bitmap) {
        virq_allocator_init();
    }
    
    spin_lock_irqsave(&virq_alloc_data.lock, flags);
    
    // Find consecutive range
    virq = bitmap_find_zero_range(virq_alloc_data.bitmap, virq_alloc_data.max_virq, count);
    
    if (virq >= virq_alloc_data.max_virq) {
        spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
        return IRQ_INVALID;
    }
    
    // Mark range as allocated
    for (uint32_t i = 0; i < count; i++) {
        bitmap_set_bit(virq_alloc_data.bitmap, virq + i);
    }
    
    // Update next_virq hint
    if ((virq + count) >= virq_alloc_data.next_virq) {
        virq_alloc_data.next_virq = virq + count;
    }
    
    spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
    
    return virq;
}

void virq_free(uint32_t virq) {
    unsigned long flags;
    
    if (virq == 0 || virq >= virq_alloc_data.max_virq) {
        return;  // Invalid virq
    }
    
    if (!virq_alloc_data.bitmap) {
        return;  // Not initialized
    }
    
    spin_lock_irqsave(&virq_alloc_data.lock, flags);
    
    // Clear allocation bit
    bitmap_clear_bit(virq_alloc_data.bitmap, virq);
    
    // Update next_virq hint for better allocation performance
    if (virq < virq_alloc_data.next_virq) {
        virq_alloc_data.next_virq = virq;
    }
    
    spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
}

void virq_free_range(uint32_t virq, uint32_t count) {
    unsigned long flags;
    
    if (virq == 0 || count == 0 || (virq + count) > virq_alloc_data.max_virq) {
        return;  // Invalid range
    }
    
    if (!virq_alloc_data.bitmap) {
        return;  // Not initialized
    }
    
    spin_lock_irqsave(&virq_alloc_data.lock, flags);
    
    // Clear range
    for (uint32_t i = 0; i < count; i++) {
        bitmap_clear_bit(virq_alloc_data.bitmap, virq + i);
    }
    
    // Update next_virq hint
    if (virq < virq_alloc_data.next_virq) {
        virq_alloc_data.next_virq = virq;
    }
    
    spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
}

bool virq_is_allocated(uint32_t virq) {
    unsigned long flags;
    bool allocated;
    
    if (virq >= virq_alloc_data.max_virq) {
        return false;
    }
    
    if (!virq_alloc_data.bitmap) {
        return false;
    }
    
    spin_lock_irqsave(&virq_alloc_data.lock, flags);
    allocated = bitmap_test_bit(virq_alloc_data.bitmap, virq);
    spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
    
    return allocated;
}

uint32_t virq_get_allocated_count(void) {
    unsigned long flags;
    uint32_t count = 0;
    
    if (!virq_alloc_data.bitmap) {
        return 0;
    }
    
    spin_lock_irqsave(&virq_alloc_data.lock, flags);
    
    for (uint32_t i = 0; i < virq_alloc_data.max_virq; i++) {
        if (bitmap_test_bit(virq_alloc_data.bitmap, i)) {
            count++;
        }
    }
    
    spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
    
    return count;
}

uint32_t virq_get_max_allocated(void) {
    unsigned long flags;
    uint32_t max_allocated = 0;
    
    if (!virq_alloc_data.bitmap) {
        return 0;
    }
    
    spin_lock_irqsave(&virq_alloc_data.lock, flags);
    
    for (uint32_t i = virq_alloc_data.max_virq - 1; i > 0; i--) {
        if (bitmap_test_bit(virq_alloc_data.bitmap, i)) {
            max_allocated = i;
            break;
        }
    }
    
    spin_unlock_irqrestore(&virq_alloc_data.lock, flags);
    
    return max_allocated;
}