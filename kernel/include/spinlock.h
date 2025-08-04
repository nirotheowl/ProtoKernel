/*
 * kernel/include/spinlock.h
 *
 * Simple spinlock implementation for ARM64
 * Uses compiler built-in atomic operations
 */

#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

#include <stdint.h>

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INITIALIZER    { 0 }

// Initialize a spinlock
static inline void spin_lock_init(spinlock_t *lock) {
    lock->lock = 0;
}

// Acquire the spinlock
static inline void spin_lock(spinlock_t *lock) {
    uint32_t tmp;
    uint32_t newval = 1;
    
    __asm__ volatile(
        "1:     ldaxr   %w0, %1\n"      // Load exclusive with acquire
        "       cbnz    %w0, 2f\n"      // If not zero, spin
        "       stxr    %w0, %w2, %1\n" // Try to store exclusive
        "       cbnz    %w0, 1b\n"      // If failed, retry
        "       b       3f\n"
        "2:     wfe\n"                  // Wait for event
        "       b       1b\n"           // Retry
        "3:\n"
        : "=&r" (tmp), "+Q" (lock->lock)
        : "r" (newval)
        : "memory");
}

// Release the spinlock
static inline void spin_unlock(spinlock_t *lock) {
    __asm__ volatile(
        "stlr   wzr, %0\n"              // Store-release of zero
        : "=Q" (lock->lock)
        :
        : "memory");
}

// Try to acquire the spinlock without blocking
static inline int spin_trylock(spinlock_t *lock) {
    uint32_t tmp;
    uint32_t newval = 1;
    
    __asm__ volatile(
        "       ldaxr   %w0, %1\n"      // Load exclusive with acquire
        "       cbnz    %w0, 1f\n"      // If not zero, fail
        "       stxr    %w0, %w2, %1\n" // Try to store exclusive
        "1:\n"
        : "=&r" (tmp), "+Q" (lock->lock)
        : "r" (newval)
        : "memory");
    
    return tmp == 0;
}

// Check if spinlock is locked
static inline int spin_is_locked(spinlock_t *lock) {
    return lock->lock != 0;
}

#endif /* _SPINLOCK_H_ */