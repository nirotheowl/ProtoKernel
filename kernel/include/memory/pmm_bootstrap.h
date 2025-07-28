/*
 * kernel/include/memory/pmm_bootstrap.h
 * 
 * Bootstrap allocator for PMM initialization
 * Temporary allocator used only to allocate the PMM bitmap
 */

#ifndef _PMM_BOOTSTRAP_H_
#define _PMM_BOOTSTRAP_H_

#include <stdint.h>
#include <stddef.h>

/* Initialize the PMM bootstrap allocator with a memory region */
void pmm_bootstrap_init(uint64_t start_addr, uint64_t end_addr);

/* Allocate memory from the bootstrap allocator (returns physical address) */
uint64_t pmm_bootstrap_alloc(size_t size, size_t alignment);

/* Get current allocation pointer (for debugging) */
uint64_t pmm_bootstrap_current(void);

/* Get total allocated size */
size_t pmm_bootstrap_used(void);

#endif /* _PMM_BOOTSTRAP_H_ */