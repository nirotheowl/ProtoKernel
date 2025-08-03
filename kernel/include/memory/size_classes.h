/*
 * kernel/include/memory/size_classes.h
 *
 * Size class definitions for kmalloc allocator
 * Based on FreeBSD's malloc size classes with adaptations
 */

#ifndef _SIZE_CLASSES_H_
#define _SIZE_CLASSES_H_

#include <stdint.h>
#include <stddef.h>
#include <memory/vmm.h>

// Maximum allocation size handled by slab allocator
#define KMALLOC_MAX_SIZE    65536   // 64KB

// Number of size classes
#define KMALLOC_NUM_CLASSES 14

// Size class definitions
static const size_t kmalloc_size_classes[KMALLOC_NUM_CLASSES] = {
    16,
    32,
    64,
    128,
    256,
    384,
    512,
    1024,
    2048,
    4096,
    8192,
    16384,
    32768,
    65536
};

// Find the appropriate size class for a given allocation size
static inline int kmalloc_size_to_class(size_t size) {
    // Handle zero or oversized allocations
    if (size == 0 || size > KMALLOC_MAX_SIZE) {
        return -1;
    }
    
    // Binary search would be more efficient for many classes,
    // but with only 14 classes, linear search is fine
    for (int i = 0; i < KMALLOC_NUM_CLASSES; i++) {
        if (size <= kmalloc_size_classes[i]) {
            return i;
        }
    }
    
    // Should never reach here given the checks above
    return -1;
}

// Get the actual allocation size for a size class
static inline size_t kmalloc_class_to_size(int class) {
    if (class < 0 || class >= KMALLOC_NUM_CLASSES) {
        return 0;
    }
    return kmalloc_size_classes[class];
}

// Check if a size requires large allocation (direct from PMM)
static inline int kmalloc_is_large(size_t size) {
    return size > KMALLOC_MAX_SIZE;
}

// Round size up to nearest size class (for statistics/debugging)
static inline size_t kmalloc_round_size(size_t size) {
    int class = kmalloc_size_to_class(size);
    if (class < 0) {
        // For large allocations, round up to page size
        return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }
    return kmalloc_size_classes[class];
}

#endif /* _SIZE_CLASSES_H_ */