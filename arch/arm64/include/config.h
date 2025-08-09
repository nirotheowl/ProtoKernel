/*
 * arch/arm64/include/config.h
 * 
 * Platform memory configuration for ARM64 kernel
 * This allows configurable physical load addresses at compile time
 */

#ifndef _ARM64_CONFIG_H
#define _ARM64_CONFIG_H

/* Platform memory configuration */
#ifndef CONFIG_PHYS_RAM_BASE
  #define CONFIG_PHYS_RAM_BASE 0x40000000  /* Default: QEMU virt */
#endif

/* Kernel load offset from RAM base (must be 2MB aligned) */
#define CONFIG_KERNEL_OFFSET 0x200000

/* Calculate actual load address */
#define CONFIG_KERNEL_PHYS_BASE (CONFIG_PHYS_RAM_BASE + CONFIG_KERNEL_OFFSET)

/* Virtual base address (fixed for all platforms) */
#define CONFIG_KERNEL_VIRT_BASE 0xFFFF000040200000UL

#endif /* _ARM64_CONFIG_H */