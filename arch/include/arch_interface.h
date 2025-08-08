/*
 * arch/include/arch_interface.h
 *
 * Common architecture interface that all architectures must implement
 */

#ifndef _ARCH_INTERFACE_H_
#define _ARCH_INTERFACE_H_

/* Get current exception/privilege level */
int arch_get_exception_level(void);

/* Include architecture-specific headers 
 * These are found via -I arch/$(ARCH)/include in the Makefile */
#include <arch_mmu.h>
#include <arch_cache.h>
#include <arch_io.h>
#include <arch_cpu.h>

/* Future: RISC-V support
#ifdef __riscv
#include "../riscv/include/arch_mmu.h"
#include "../riscv/include/arch_cache.h"
#include "../riscv/include/arch_io.h"
#endif
*/

#endif /* _ARCH_INTERFACE_H_ */