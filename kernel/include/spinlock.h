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

#endif /* _SPINLOCK_H_ */