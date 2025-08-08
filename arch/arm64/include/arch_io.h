/*
 * arch/arm64/include/arch_io.h
 *
 * ARM64 I/O operations
 */

#ifndef _ARM64_ARCH_IO_H_
#define _ARM64_ARCH_IO_H_

/* ARM64 I/O barrier */
static inline void arch_io_barrier(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ARM64 NOP instruction */
static inline void arch_io_nop(void) {
    __asm__ volatile("nop");
}

#endif /* _ARM64_ARCH_IO_H_ */