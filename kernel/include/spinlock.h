/*
 * kernel/include/spinlock.h
 *
 * Architecture-independent spinlock interface
 */

#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

/* Get architecture-specific spinlock implementation */
#include <arch_spinlock.h>

/* The architecture provides:
 * - spinlock_t type
 * - SPINLOCK_INITIALIZER macro
 * - spin_lock_init()
 * - spin_lock()
 * - spin_unlock()
 * - spin_trylock()
 * - spin_is_locked()
 */

/* Common spinlock variants with interrupt handling */

// For now, we'll use simple implementations without actual interrupt control
// These will be enhanced when interrupt controller is fully integrated

typedef unsigned long irqflags_t;

static inline irqflags_t arch_local_irq_save(void) {
    // Placeholder - will be implemented with actual interrupt control
    return 0;
}

static inline void arch_local_irq_restore(irqflags_t flags) {
    // Placeholder - will be implemented with actual interrupt control
    (void)flags;
}

#define spin_lock_irqsave(lock, flags) do { \
    (flags) = arch_local_irq_save(); \
    spin_lock(lock); \
} while (0)

#define spin_unlock_irqrestore(lock, flags) do { \
    spin_unlock(lock); \
    arch_local_irq_restore(flags); \
} while (0)

#endif /* _SPINLOCK_H_ */