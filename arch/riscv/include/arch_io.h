/*
 * arch/riscv/include/arch_io.h
 * 
 * RISC-V I/O operations
 */

#ifndef _ARCH_IO_H_
#define _ARCH_IO_H_

#include <stdint.h>

static inline void arch_io_barrier(void) {
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline void arch_io_nop(void) {
    __asm__ volatile("nop");
}

// Memory-mapped I/O read operations
static inline uint8_t mmio_read8(const volatile void *addr) {
    uint8_t val;
    __asm__ volatile("lb %0, 0(%1)" : "=r"(val) : "r"(addr));
    __asm__ volatile("fence i, r" ::: "memory");
    return val;
}

static inline uint16_t mmio_read16(const volatile void *addr) {
    uint16_t val;
    __asm__ volatile("lh %0, 0(%1)" : "=r"(val) : "r"(addr));
    __asm__ volatile("fence i, r" ::: "memory");
    return val;
}

static inline uint32_t mmio_read32(const volatile void *addr) {
    uint32_t val;
    __asm__ volatile("lw %0, 0(%1)" : "=r"(val) : "r"(addr));
    __asm__ volatile("fence i, r" ::: "memory");
    return val;
}

static inline uint64_t mmio_read64(const volatile void *addr) {
    uint64_t val;
    __asm__ volatile("ld %0, 0(%1)" : "=r"(val) : "r"(addr));
    __asm__ volatile("fence i, r" ::: "memory");
    return val;
}

// Memory-mapped I/O write operations
static inline void mmio_write8(volatile void *addr, uint8_t val) {
    __asm__ volatile("fence w, o" ::: "memory");
    __asm__ volatile("sb %0, 0(%1)" :: "r"(val), "r"(addr));
}

static inline void mmio_write16(volatile void *addr, uint16_t val) {
    __asm__ volatile("fence w, o" ::: "memory");
    __asm__ volatile("sh %0, 0(%1)" :: "r"(val), "r"(addr));
}

static inline void mmio_write32(volatile void *addr, uint32_t val) {
    __asm__ volatile("fence w, o" ::: "memory");
    __asm__ volatile("sw %0, 0(%1)" :: "r"(val), "r"(addr));
}

static inline void mmio_write64(volatile void *addr, uint64_t val) {
    __asm__ volatile("fence w, o" ::: "memory");
    __asm__ volatile("sd %0, 0(%1)" :: "r"(val), "r"(addr));
}

#endif /* _ARCH_IO_H_ */