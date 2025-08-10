/*
 * arch/riscv/include/arch_spinlock.h
 * 
 * RISC-V spinlock operations
 */

#ifndef _ARCH_SPINLOCK_H_
#define _ARCH_SPINLOCK_H_

#include <stdint.h>

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INITIALIZER { 0 }

static inline void spin_lock_init(spinlock_t *lock) {
    lock->lock = 0;
}

static inline void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        __asm__ volatile("nop" ::: "memory");
    }
}

static inline void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->lock);
}

static inline int spin_trylock(spinlock_t *lock) {
    return !__sync_lock_test_and_set(&lock->lock, 1);
}

static inline int spin_is_locked(spinlock_t *lock) {
    return lock->lock != 0;
}

#endif /* _ARCH_SPINLOCK_H_ */