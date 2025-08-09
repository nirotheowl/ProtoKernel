/*
 * kernel/include/memory/vmparam.h
 *
 * Virtual memory parameters - architecture independent interface
 * Following FreeBSD conventions where applicable
 */

#ifndef _MEMORY_VMPARAM_H_
#define _MEMORY_VMPARAM_H_

#include <stdint.h>

// Include architecture-specific virtual memory parameters
#include <arch_vmparam.h>

// Map architecture-specific constants to generic names
#define KERNEL_VIRT_BASE    ARCH_KERNEL_VIRT_BASE
#define DEVICE_VIRT_BASE    ARCH_DEVICE_VIRT_BASE
#define DMAP_BASE           ARCH_DMAP_VIRT_BASE
#define DMAP_SIZE           ARCH_DMAP_SIZE

// Kernel physical base - dynamically detected at boot time
extern uint64_t kernel_phys_base;

// For backward compatibility during transition
#define KERNEL_PHYS_BASE    kernel_phys_base

// Convert kernel virtual address to physical address
#define PHYS_TO_VIRT(pa) \
    ((uint64_t)(pa) - kernel_phys_base + KERNEL_VIRT_BASE)

// Convert physical address to kernel virtual address
#define VIRT_TO_PHYS(va) \
    ((uint64_t)(va) - KERNEL_VIRT_BASE + kernel_phys_base)

// Check if an address is in kernel virtual space
#define VIRT_IN_KERN(va) \
    ((uint64_t)(va) >= KERNEL_VIRT_BASE)

// Physical memory base - now dynamic, set at runtime
extern uint64_t dmap_phys_base;
extern uint64_t dmap_phys_max;

// Convert physical address to DMAP virtual address
#define PHYS_TO_DMAP(pa)    ({ \
    uint64_t _pa = (uint64_t)(pa); \
    (_pa >= dmap_phys_base && _pa < dmap_phys_max) ? \
        (DMAP_BASE + (_pa - dmap_phys_base)) : 0; \
})

// Convert DMAP virtual address to physical address
#define DMAP_TO_PHYS(va)    ({ \
    uint64_t _va = (uint64_t)(va); \
    (_va >= DMAP_BASE && _va < (DMAP_BASE + (dmap_phys_max - dmap_phys_base))) ? \
        (_va - DMAP_BASE + dmap_phys_base) : 0; \
})

// Check if address is in DMAP range
#define VIRT_IN_DMAP(va)    ((uint64_t)(va) >= DMAP_BASE && \
                             (uint64_t)(va) < (DMAP_BASE + DMAP_SIZE))

#endif // _MEMORY_VMPARAM_H_