/*
 * arch/riscv/include/arch_vmparam.h
 * 
 * RISC-V virtual memory parameters
 */

#ifndef _ARCH_VMPARAM_H_
#define _ARCH_VMPARAM_H_

// RISC-V Sv39 virtual memory layout
#define VM_MIN_KERNEL_ADDRESS   0xFFFFFFFF80000000UL
#define VM_MAX_KERNEL_ADDRESS   0xFFFFFFFFFFFFFFFFUL

#define ARCH_KERNEL_VIRT_BASE   0xFFFFFFFF80200000UL
#define ARCH_DMAP_VIRT_BASE     0xFFFFFFC000000000UL  // Direct map region

// Page sizes
#define ARCH_PAGE_SHIFT         12
#define ARCH_PAGE_SIZE          (1UL << ARCH_PAGE_SHIFT)
#define ARCH_PAGE_MASK          (ARCH_PAGE_SIZE - 1)

// Large page sizes
#define L2_SIZE                 (1UL << 21)  // 2MB
#define L1_SIZE                 (1UL << 30)  // 1GB

// Kernel pre-mapped region size (set up by boot.S)
// RISC-V boot.S maps 16MB for the kernel
#define ARCH_KERNEL_PREMAPPED_SIZE  0x1000000ULL  /* 16MB */

#endif /* _ARCH_VMPARAM_H_ */