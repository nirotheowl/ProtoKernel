/*
 * kernel/exceptions/exceptions.c
 *
 * Exception and interrupt handling implementation
 */

#include <stdint.h>
#include <arch_exceptions.h>
#include <uart.h>

// External assembly function
extern void install_exception_vectors(void);

static void print_hex(const char* label, uint64_t value) {
    uart_puts(label);
    uart_puts(": 0x");
    uart_puthex(value);
    uart_puts("\n");
}

const char* get_exception_class_string(uint64_t esr) {
    uint32_t ec = (esr & ESR_EC_MASK) >> ESR_EC_SHIFT;
    
    switch (ec) {
        case ESR_EC_UNKNOWN:          return "Unknown reason";
        case ESR_EC_WFI_WFE:          return "WFI/WFE instruction";
        case ESR_EC_MCR_MRC_CP15:     return "MCR/MRC CP15";
        case ESR_EC_MCRR_MRRC_CP15:   return "MCRR/MRRC CP15";
        case ESR_EC_MCR_MRC_CP14:     return "MCR/MRC CP14";
        case ESR_EC_LDC_STC_CP14:     return "LDC/STC CP14";
        case ESR_EC_FP_ASIMD:         return "FP/ASIMD access";
        case ESR_EC_MCR_MRC_CP10:     return "MCR/MRC CP10";
        case ESR_EC_MRRC_CP14:        return "MRRC CP14";
        case ESR_EC_ILLEGAL:          return "Illegal execution state";
        case ESR_EC_SVC_A64:          return "SVC (AArch64)";
        case ESR_EC_SYS_REG:          return "System register access";
        case ESR_EC_INST_ABORT_EL0:   return "Instruction abort (lower EL)";
        case ESR_EC_INST_ABORT_EL1:   return "Instruction abort (current EL)";
        case ESR_EC_PC_ALIGN:         return "PC alignment fault";
        case ESR_EC_DATA_ABORT_EL0:   return "Data abort (lower EL)";
        case ESR_EC_DATA_ABORT_EL1:   return "Data abort (current EL)";
        case ESR_EC_SP_ALIGN:         return "SP alignment fault";
        case ESR_EC_FP_EXC32:         return "FP exception (AArch32)";
        case ESR_EC_FP_EXC64:         return "FP exception (AArch64)";
        case ESR_EC_SERROR:           return "SError interrupt";
        case ESR_EC_BREAKPOINT_EL0:   return "Breakpoint (lower EL)";
        case ESR_EC_BREAKPOINT_EL1:   return "Breakpoint (current EL)";
        case ESR_EC_SSTEP_EL0:        return "Single step (lower EL)";
        case ESR_EC_SSTEP_EL1:        return "Single step (current EL)";
        case ESR_EC_WATCHPOINT_EL0:   return "Watchpoint (lower EL)";
        case ESR_EC_WATCHPOINT_EL1:   return "Watchpoint (current EL)";
        case ESR_EC_BKPT_INS:         return "BKPT instruction";
        case ESR_EC_BRK_INS:          return "BRK instruction";
        default:                      return "Unknown exception class";
    }
}

const char* get_fault_status_string(uint64_t fsc) {
    switch (fsc & 0x3F) {
        case FSC_ADDR_SIZE_L0:        return "Address size fault, level 0";
        case FSC_ADDR_SIZE_L1:        return "Address size fault, level 1";
        case FSC_ADDR_SIZE_L2:        return "Address size fault, level 2";
        case FSC_ADDR_SIZE_L3:        return "Address size fault, level 3";
        case FSC_TRANSLATION_L0:      return "Translation fault, level 0";
        case FSC_TRANSLATION_L1:      return "Translation fault, level 1";
        case FSC_TRANSLATION_L2:      return "Translation fault, level 2";
        case FSC_TRANSLATION_L3:      return "Translation fault, level 3";
        case FSC_ACCESS_FLAG_L1:      return "Access flag fault, level 1";
        case FSC_ACCESS_FLAG_L2:      return "Access flag fault, level 2";
        case FSC_ACCESS_FLAG_L3:      return "Access flag fault, level 3";
        case FSC_PERMISSION_L1:       return "Permission fault, level 1";
        case FSC_PERMISSION_L2:       return "Permission fault, level 2";
        case FSC_PERMISSION_L3:       return "Permission fault, level 3";
        case FSC_SYNC_EXTERNAL:       return "Synchronous external abort";
        case FSC_SYNC_EXTERNAL_TT_L0: return "Sync external abort on table walk, level 0";
        case FSC_SYNC_EXTERNAL_TT_L1: return "Sync external abort on table walk, level 1";
        case FSC_SYNC_EXTERNAL_TT_L2: return "Sync external abort on table walk, level 2";
        case FSC_SYNC_EXTERNAL_TT_L3: return "Sync external abort on table walk, level 3";
        case FSC_SYNC_PARITY:         return "Synchronous parity error";
        case FSC_ASYNC_EXTERNAL:      return "Asynchronous external abort";
        case FSC_ASYNC_PARITY:        return "Asynchronous parity error";
        case FSC_ALIGNMENT:           return "Alignment fault";
        case FSC_TLB_CONFLICT:        return "TLB conflict abort";
        case FSC_LOCKDOWN:            return "Lockdown abort";
        case FSC_UNSUPPORTED_EXCL:    return "Unsupported exclusive access";
        case FSC_TAG_CHECK:           return "Tag check fault";
        default:                      return "Unknown fault status";
    }
}

void dump_exception_context(struct exception_context *ctx) {
    uart_puts("\n=== Exception Context ===\n");
    
    // Exception registers
    print_hex("ELR_EL1", ctx->elr);
    print_hex("SPSR_EL1", ctx->spsr);
    print_hex("ESR_EL1", ctx->esr);
    print_hex("FAR_EL1", ctx->far);
    
    // Parse ESR
    uint32_t ec = (ctx->esr & ESR_EC_MASK) >> ESR_EC_SHIFT;
    uart_puts("Exception class: ");
    uart_puts(get_exception_class_string(ctx->esr));
    uart_puts(" (0x");
    uart_puthex(ec);
    uart_puts(")\n");
    
    // For data/instruction aborts, show fault status
    if (ec == ESR_EC_DATA_ABORT_EL0 || ec == ESR_EC_DATA_ABORT_EL1 ||
        ec == ESR_EC_INST_ABORT_EL0 || ec == ESR_EC_INST_ABORT_EL1) {
        uint32_t fsc = ctx->esr & ESR_ISS_DFSC_MASK;
        uart_puts("Fault status: ");
        uart_puts(get_fault_status_string(fsc));
        uart_puts(" (0x");
        uart_puthex(fsc);
        uart_puts(")\n");
        
        if (ctx->esr & ESR_ISS_WNR) {
            uart_puts("Access type: Write\n");
        } else {
            uart_puts("Access type: Read\n");
        }
        
        if (!(ctx->esr & ESR_ISS_FNV)) {
            uart_puts("Fault address: 0x");
            uart_puthex(ctx->far);
            uart_puts("\n");
        }
    }
    
    // Key registers
    uart_puts("\nKey registers:\n");
    print_hex("  X0", ctx->x0);
    print_hex("  X1", ctx->x1);
    print_hex("  X2", ctx->x2);
    print_hex("  X3", ctx->x3);
    print_hex(" X29 (FP)", ctx->x29);
    print_hex(" X30 (LR)", ctx->x30);
    print_hex("  SP", ctx->sp);
}

// Synchronous exception handler
void sync_exception_handler(struct exception_context *ctx) {
    uart_puts("\n!!! SYNCHRONOUS EXCEPTION !!!\n");
    dump_exception_context(ctx);
    
    uint32_t ec = (ctx->esr & ESR_EC_MASK) >> ESR_EC_SHIFT;
    
    // Handle specific exception types
    switch (ec) {
        case ESR_EC_DATA_ABORT_EL0:
        case ESR_EC_DATA_ABORT_EL1:
            uart_puts("\nData abort - possible page fault\n");
            // Here you would implement page fault handling
            // For now, just halt
            // FIXME: implement feature 
            break;
            
        case ESR_EC_INST_ABORT_EL0:
        case ESR_EC_INST_ABORT_EL1:
            uart_puts("\nInstruction abort - possible page fault\n");
            // Here you would implement page fault handling
            // For now, just halt
            // FIXME: implement feature
            break;
            
        case ESR_EC_SVC_A64:
            uart_puts("\nSystem call (not implemented)\n");
            break;
            
        default:
            uart_puts("\nUnhandled exception type\n");
            break;
    }
    
    uart_puts("\nSystem halted due to exception\n");
    while (1) {
        __asm__ volatile("wfe");
    }
}

// IRQ handler
void irq_handler(struct exception_context *ctx) {
    uart_puts("\n!!! IRQ !!!\n");
    dump_exception_context(ctx);
    uart_puts("IRQ handling not implemented\n");
}

// FIQ handler
void fiq_handler(struct exception_context *ctx) {
    uart_puts("\n!!! FIQ !!!\n");
    dump_exception_context(ctx);
    uart_puts("FIQ handling not implemented\n");
}

// SError handler
void serror_handler(struct exception_context *ctx) {
    uart_puts("\n!!! SERROR !!!\n");
    dump_exception_context(ctx);
    uart_puts("\nSystem error - halting\n");
    while (1) {
        __asm__ volatile("wfe");
    }
}

// Initialize exception handling
void exception_init(void) {
    uart_puts("Installing exception vectors...\n");
    install_exception_vectors();
    uart_puts("Exception vectors installed at VBAR_EL1\n");
    
    // Verify VBAR_EL1
    uint64_t vbar;
    __asm__ volatile("mrs %0, vbar_el1" : "=r" (vbar));
    print_hex("VBAR_EL1", vbar);
}
