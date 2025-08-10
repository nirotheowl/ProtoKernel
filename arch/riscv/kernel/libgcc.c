/*
 * arch/riscv/kernel/libgcc.c
 * 
 * Minimal libgcc functions for RISC-V
 */

#include <stdint.h>

// Byte swap for 32-bit values
uint32_t __bswapsi2(uint32_t x) {
    return ((x & 0x000000ff) << 24) |
           ((x & 0x0000ff00) << 8)  |
           ((x & 0x00ff0000) >> 8)  |
           ((x & 0xff000000) >> 24);
}

// Byte swap for 64-bit values
uint64_t __bswapdi2(uint64_t x) {
    return ((x & 0x00000000000000ffULL) << 56) |
           ((x & 0x000000000000ff00ULL) << 40) |
           ((x & 0x0000000000ff0000ULL) << 24) |
           ((x & 0x00000000ff000000ULL) << 8)  |
           ((x & 0x000000ff00000000ULL) >> 8)  |
           ((x & 0x0000ff0000000000ULL) >> 24) |
           ((x & 0x00ff000000000000ULL) >> 40) |
           ((x & 0xff00000000000000ULL) >> 56);
}