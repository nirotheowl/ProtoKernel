/*
 * kernel/include/exceptions/exceptions.h
 *
 * Exception handling constants and interfaces
 */

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <stdint.h>

// Exception Syndrome Register (ESR_EL1) fields
#define ESR_EC_SHIFT            26
#define ESR_EC_MASK             (0x3FUL << ESR_EC_SHIFT)

// Exception Class values
#define ESR_EC_UNKNOWN          0x00
#define ESR_EC_WFI_WFE          0x01
#define ESR_EC_MCR_MRC_CP15     0x03
#define ESR_EC_MCRR_MRRC_CP15   0x04
#define ESR_EC_MCR_MRC_CP14     0x05
#define ESR_EC_LDC_STC_CP14     0x06
#define ESR_EC_FP_ASIMD         0x07
#define ESR_EC_MCR_MRC_CP10     0x08
#define ESR_EC_MRRC_CP14        0x0C
#define ESR_EC_ILLEGAL          0x0E
#define ESR_EC_SVC_A64          0x15
#define ESR_EC_SYS_REG          0x18
#define ESR_EC_INST_ABORT_EL0   0x20
#define ESR_EC_INST_ABORT_EL1   0x21
#define ESR_EC_PC_ALIGN         0x22
#define ESR_EC_DATA_ABORT_EL0   0x24
#define ESR_EC_DATA_ABORT_EL1   0x25
#define ESR_EC_SP_ALIGN         0x26
#define ESR_EC_FP_EXC32         0x28
#define ESR_EC_FP_EXC64         0x2C
#define ESR_EC_SERROR           0x2F
#define ESR_EC_BREAKPOINT_EL0   0x30
#define ESR_EC_BREAKPOINT_EL1   0x31
#define ESR_EC_SSTEP_EL0        0x32
#define ESR_EC_SSTEP_EL1        0x33
#define ESR_EC_WATCHPOINT_EL0   0x34
#define ESR_EC_WATCHPOINT_EL1   0x35
#define ESR_EC_BKPT_INS         0x38
#define ESR_EC_BRK_INS          0x3C

// Data/Instruction Abort ISS encoding
#define ESR_ISS_DFSC_MASK       0x3F
#define ESR_ISS_WNR             (1UL << 6)   // Write not Read
#define ESR_ISS_S1PTW           (1UL << 7)   // Stage 1 translation table walk
#define ESR_ISS_CM              (1UL << 8)   // Cache maintenance
#define ESR_ISS_EA              (1UL << 9)   // External abort
#define ESR_ISS_FNV             (1UL << 10)  // FAR not valid
#define ESR_ISS_SET_SHIFT       11
#define ESR_ISS_SET_MASK        (0x3UL << ESR_ISS_SET_SHIFT)
#define ESR_ISS_VNCR            (1UL << 13)
#define ESR_ISS_AR              (1UL << 14)  // Acquire/Release
#define ESR_ISS_SF              (1UL << 15)  // Sixty-Four bit register
#define ESR_ISS_SRT_SHIFT       16
#define ESR_ISS_SRT_MASK        (0x1FUL << ESR_ISS_SRT_SHIFT)
#define ESR_ISS_SSE             (1UL << 21)  // Syndrome sign extend
#define ESR_ISS_SAS_SHIFT       22
#define ESR_ISS_SAS_MASK        (0x3UL << ESR_ISS_SAS_SHIFT)
#define ESR_ISS_ISV             (1UL << 24)  // Instruction syndrome valid

// Fault Status Code values (Data/Instruction Abort)
#define FSC_ADDR_SIZE_L0        0x00
#define FSC_ADDR_SIZE_L1        0x01
#define FSC_ADDR_SIZE_L2        0x02
#define FSC_ADDR_SIZE_L3        0x03
#define FSC_TRANSLATION_L0      0x04
#define FSC_TRANSLATION_L1      0x05
#define FSC_TRANSLATION_L2      0x06
#define FSC_TRANSLATION_L3      0x07
#define FSC_ACCESS_FLAG_L1      0x09
#define FSC_ACCESS_FLAG_L2      0x0A
#define FSC_ACCESS_FLAG_L3      0x0B
#define FSC_PERMISSION_L1       0x0D
#define FSC_PERMISSION_L2       0x0E
#define FSC_PERMISSION_L3       0x0F
#define FSC_SYNC_EXTERNAL       0x10
#define FSC_SYNC_EXTERNAL_TT_L0 0x14
#define FSC_SYNC_EXTERNAL_TT_L1 0x15
#define FSC_SYNC_EXTERNAL_TT_L2 0x16
#define FSC_SYNC_EXTERNAL_TT_L3 0x17
#define FSC_SYNC_PARITY         0x18
#define FSC_ASYNC_EXTERNAL      0x11
#define FSC_ASYNC_PARITY        0x19
#define FSC_ALIGNMENT           0x21
#define FSC_TLB_CONFLICT        0x30
#define FSC_LOCKDOWN            0x34
#define FSC_UNSUPPORTED_EXCL    0x35
#define FSC_TAG_CHECK           0x3D

// Exception context structure
struct exception_context {
    uint64_t elr;       // Exception Link Register
    uint64_t spsr;      // Saved Program Status Register
    uint64_t esr;       // Exception Syndrome Register
    uint64_t far;       // Fault Address Register
    // General purpose registers
    uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
    uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
    uint64_t x16, x17, x18, x19, x20, x21, x22, x23;
    uint64_t x24, x25, x26, x27, x28, x29, x30;
    uint64_t sp;
};

// Exception handler functions
void exception_init(void);
void sync_exception_handler(struct exception_context *ctx);
void irq_handler(struct exception_context *ctx);
void fiq_handler(struct exception_context *ctx);
void serror_handler(struct exception_context *ctx);

// Helper functions
const char* get_exception_class_string(uint64_t esr);
const char* get_fault_status_string(uint64_t fsc);
void dump_exception_context(struct exception_context *ctx);

#endif // EXCEPTIONS_H