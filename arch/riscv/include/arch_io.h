/*
 * arch/riscv/include/arch_io.h
 * 
 * RISC-V I/O operations
 */

#ifndef _ARCH_IO_H_
#define _ARCH_IO_H_

static inline void arch_io_barrier(void) {
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline void arch_io_nop(void) {
    __asm__ volatile("nop");
}

#endif /* _ARCH_IO_H_ */