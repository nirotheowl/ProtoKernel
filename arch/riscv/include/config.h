/*
 * arch/riscv/include/config.h
 * 
 * Platform memory configuration for RISC-V kernel
 * This allows configurable physical load addresses at compile time
 */

#ifndef _RISCV_CONFIG_H
#define _RISCV_CONFIG_H

// Platform memory configuration
#ifndef CONFIG_PHYS_RAM_BASE
  #define CONFIG_PHYS_RAM_BASE 0x80000000  // Default: QEMU virt RISC-V
#endif

// Kernel load offset from RAM base
// RISC-V QEMU loads kernel at 0x80200000 by default (2MB offset)
#define CONFIG_KERNEL_OFFSET 0x200000

// Calculate actual load address
#define CONFIG_KERNEL_PHYS_BASE (CONFIG_PHYS_RAM_BASE + CONFIG_KERNEL_OFFSET)

// Virtual base address for RISC-V (in Sv39 upper half)
// Using 0xFFFFFFFF80000000 range for kernel space
#define CONFIG_KERNEL_VIRT_BASE 0xFFFFFFFF80200000UL

// Page size configuration
#define CONFIG_PAGE_SIZE 4096
#define CONFIG_PAGE_SHIFT 12

// Stack size for initial boot stack
#define CONFIG_BOOT_STACK_SIZE 0x10000  // 64KB

#endif /* _RISCV_CONFIG_H */