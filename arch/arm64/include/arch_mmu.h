/*
 * arch/arm64/include/arch_mmu.h
 *
 * ARM64 MMU operations
 */

#ifndef _ARM64_ARCH_MMU_H_
#define _ARM64_ARCH_MMU_H_

#include <stdint.h>

/* ARM64 MMU memory barrier */
static inline void arch_mmu_barrier(void) {
    __asm__ volatile("dsb ishst" ::: "memory");
}

/* Get TTBR1_EL1 register value */
static inline uint64_t arch_mmu_get_ttbr1(void) {
    uint64_t ttbr1;
    __asm__ volatile("mrs %0, TTBR1_EL1" : "=r"(ttbr1));
    return ttbr1;
}

/* Invalidate TLB entry for a specific virtual address */
static inline void arch_mmu_invalidate_page(uint64_t vaddr) {
    __asm__ volatile(
        "tlbi vae1is, %0\n"
        "dsb ish\n"
        "isb\n"
        : : "r"(vaddr >> 12) : "memory");
}

/* Flush entire TLB */
static inline void arch_mmu_flush_all(void) {
    __asm__ volatile(
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        : : : "memory");
}

#endif /* _ARM64_ARCH_MMU_H_ */