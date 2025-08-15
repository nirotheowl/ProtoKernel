/*
 * arch/arm64/include/arch_io.h
 *
 * ARM64 I/O operations
 */

#ifndef _ARM64_ARCH_IO_H_
#define _ARM64_ARCH_IO_H_

#include <stdint.h>

// ARM64 I/O barrier
static inline void arch_io_barrier(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

// ARM64 NOP instruction
static inline void arch_io_nop(void) {
    __asm__ volatile("nop");
}

// Memory-mapped I/O read operations
static inline uint8_t mmio_read8(const volatile void *addr) {
    uint8_t val;
    __asm__ volatile("ldrb %w0, [%1]" : "=r"(val) : "r"(addr));
    return val;
}

static inline uint16_t mmio_read16(const volatile void *addr) {
    uint16_t val;
    __asm__ volatile("ldrh %w0, [%1]" : "=r"(val) : "r"(addr));
    return val;
}

static inline uint32_t mmio_read32(const volatile void *addr) {
    uint32_t val;
    __asm__ volatile("ldr %w0, [%1]" : "=r"(val) : "r"(addr));
    return val;
}

static inline uint64_t mmio_read64(const volatile void *addr) {
    uint64_t val;
    __asm__ volatile("ldr %0, [%1]" : "=r"(val) : "r"(addr));
    return val;
}

// Memory-mapped I/O write operations
static inline void mmio_write8(volatile void *addr, uint8_t val) {
    __asm__ volatile("strb %w0, [%1]" :: "r"(val), "r"(addr));
}

static inline void mmio_write16(volatile void *addr, uint16_t val) {
    __asm__ volatile("strh %w0, [%1]" :: "r"(val), "r"(addr));
}

static inline void mmio_write32(volatile void *addr, uint32_t val) {
    __asm__ volatile("str %w0, [%1]" :: "r"(val), "r"(addr));
}

static inline void mmio_write64(volatile void *addr, uint64_t val) {
    __asm__ volatile("str %0, [%1]" :: "r"(val), "r"(addr));
}

#endif // _ARM64_ARCH_IO_H_