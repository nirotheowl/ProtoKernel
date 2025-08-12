/*
 * arch/riscv/include/arch_mmu.h
 * 
 * RISC-V MMU operations (stub for initial testing)
 */

#ifndef _ARCH_MMU_H_
#define _ARCH_MMU_H_

#include <stdint.h>

// RISC-V MMU operations
static inline void arch_mmu_barrier(void) {
    // Memory fence for read-write ordering, NOT a TLB flush
    __asm__ volatile("fence rw,rw" ::: "memory");
}

static inline uint64_t arch_mmu_get_ttbr1(void) {
    // RISC-V doesn't have TTBR1 - this is for compatibility
    return 0;
}

static inline void arch_mmu_invalidate_page(uint64_t vaddr) {
    __asm__ volatile("sfence.vma %0, zero" : : "r"(vaddr) : "memory");
}

static inline void arch_mmu_flush_all(void) {
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
}

#endif /* _ARCH_MMU_H_ */