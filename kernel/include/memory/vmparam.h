/*
 * kernel/include/memory/vmparam.h
 *
 * Virtual memory parameters for ARM64
 * Following FreeBSD conventions where applicable
 */

#ifndef _MEMORY_VMPARAM_H_
#define _MEMORY_VMPARAM_H_

#include <stdint.h>


// Kernel virtual and physical base addresses
#define KERNEL_VIRT_BASE    0xFFFF000040200000ULL
#define KERNEL_PHYS_BASE    0x40200000ULL

// For now, we use a simple direct mapping for kernel space.
// The kernel is loaded at KERNEL_PHYS_BASE and mapped to KERNEL_VIRT_BASE.
// 
// Unlike FreeBSD's separate DMAP region, we currently map:
// - Kernel code/data: KERNEL_PHYS_BASE -> KERNEL_VIRT_BASE
// - Device memory: Physical addresses -> 0xFFFF0000xxxxxxxx

// Convert kernel virtual address to physical address
#define PHYS_TO_VIRT(pa) \
    ((uint64_t)(pa) - KERNEL_PHYS_BASE + KERNEL_VIRT_BASE)

// Convert physical address to kernel virtual address
#define VIRT_TO_PHYS(va) \
    ((uint64_t)(va) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)

// Check if an address is in kernel virtual space
#define VIRT_IN_KERN(va) \
    ((uint64_t)(va) >= KERNEL_VIRT_BASE)

// Device memory base in virtual space
#define DEVICE_VIRT_BASE    0xFFFF000000000000ULL

// Physical memory base - now dynamic, set at runtime
extern uint64_t dmap_phys_base;
extern uint64_t dmap_phys_max;

// DMAP (Direct Map) region configuration
#define DMAP_BASE           0xFFFFA00000000000ULL  /* Direct map base address (FreeBSD standard) */
#define DMAP_SIZE           0x0000600000000000ULL  /* 96TB direct map region */

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