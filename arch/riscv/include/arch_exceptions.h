/*
 * arch/riscv/include/arch_exceptions.h
 * 
 * RISC-V exception handling definitions
 */

#ifndef _ARCH_EXCEPTIONS_H_
#define _ARCH_EXCEPTIONS_H_

#include <stdint.h>

// RISC-V exception context
typedef struct {
    uint64_t ra;      // Return address
    uint64_t sp;      // Stack pointer
    uint64_t gp;      // Global pointer
    uint64_t tp;      // Thread pointer
    uint64_t t0, t1, t2;  // Temporaries
    uint64_t s0, s1;      // Saved registers
    uint64_t a0, a1, a2, a3, a4, a5, a6, a7;  // Arguments
    uint64_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;  // More saved
    uint64_t t3, t4, t5, t6;  // More temporaries
    uint64_t sepc;    // Exception PC
    uint64_t sstatus; // Status register
} arch_context_t;

void arch_install_exception_handlers(void);
int arch_get_exception_level(void);

#endif /* _ARCH_EXCEPTIONS_H_ */