/*
 * arch/arm64/include/arch_vmparam.h
 *
 * ARM64-specific virtual memory parameters
 */

#ifndef _ARM64_ARCH_VMPARAM_H_
#define _ARM64_ARCH_VMPARAM_H_

/*
 * ARM64 Virtual Address Space Layout (48-bit VA)
 * 
 * User space:   0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF (256TB)
 * Kernel space: 0xFFFF_0000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF (256TB)
 * 
 * The address space is split using TTBR0 (user) and TTBR1 (kernel).
 * Addresses with bits [63:48] all zeros use TTBR0.
 * Addresses with bits [63:48] all ones use TTBR1.
 */

// Kernel virtual base address (where kernel .text starts)
// This is in the higher half, using TTBR1
#define ARCH_KERNEL_VIRT_BASE    0xFFFF000040200000ULL

// Physical memory direct map base
// Used to map all physical RAM with a simple offset
#define ARCH_DMAP_VIRT_BASE      0xFFFFA00000000000ULL

// DMAP size - amount of virtual space reserved for direct map
// For ARM64: 96TB is available for DMAP
#define ARCH_DMAP_SIZE           0x0000600000000000ULL

// Device memory mapping base
// Used for memory-mapped I/O devices
#define ARCH_DEVICE_VIRT_BASE    0xFFFF000100000000ULL

// Kernel heap base (for dynamic allocations)
#define ARCH_KHEAP_VIRT_BASE     0xFFFF800000000000ULL

// Maximum physical address supported (48-bit)
#define ARCH_MAX_PHYS_ADDR       0x0000FFFFFFFFFFFFULL

// Page table level count for ARM64
#define ARCH_PAGE_TABLE_LEVELS   4

// Virtual address bits
#define ARCH_VA_BITS             48

// Kernel pre-mapped region size (set up by boot.S)
// boot.S maps 128MB (64 * 2MB blocks) for the kernel
#define ARCH_KERNEL_PREMAPPED_SIZE  0x8000000ULL  /* 128MB */

#endif // _ARM64_ARCH_VMPARAM_H_