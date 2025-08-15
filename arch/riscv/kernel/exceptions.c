/*
 * arch/riscv/kernel/exceptions.c
 * 
 * RISC-V exception handling
 */

#include <arch_exceptions.h>
#include <stdint.h>
#include <panic.h>
#include <uart.h>
#include <irqchip/riscv-intc.h>
#include <irqchip/riscv-plic.h>

// External trap vector from trap.S
extern void trap_vector(void);
extern void early_trap_vector(void);

// Trap cause definitions
#define CAUSE_INTERRUPT_BIT (1UL << 63)
#define CAUSE_EXCEPTION_CODE(cause) ((cause) & 0x7FFFFFFFFFFFFFFF)

// Exception codes
#define EXC_INST_MISALIGNED     0
#define EXC_INST_ACCESS_FAULT   1
#define EXC_ILLEGAL_INST        2
#define EXC_BREAKPOINT          3
#define EXC_LOAD_MISALIGNED     4
#define EXC_LOAD_ACCESS_FAULT   5
#define EXC_STORE_MISALIGNED    6
#define EXC_STORE_ACCESS_FAULT  7
#define EXC_ECALL_U             8
#define EXC_ECALL_S             9
#define EXC_ECALL_M             11
#define EXC_INST_PAGE_FAULT     12
#define EXC_LOAD_PAGE_FAULT     13
#define EXC_STORE_PAGE_FAULT    15

// Interrupt codes
#define INT_SOFTWARE_S          1
#define INT_SOFTWARE_M          3
#define INT_TIMER_S             5
#define INT_TIMER_M             7
#define INT_EXTERNAL_S          9
#define INT_EXTERNAL_M          11

// Convert exception code to string
static const char *exception_to_string(uint64_t code) {
    switch (code) {
    case EXC_INST_MISALIGNED:    return "Instruction address misaligned";
    case EXC_INST_ACCESS_FAULT:  return "Instruction access fault";
    case EXC_ILLEGAL_INST:       return "Illegal instruction";
    case EXC_BREAKPOINT:         return "Breakpoint";
    case EXC_LOAD_MISALIGNED:    return "Load address misaligned";
    case EXC_LOAD_ACCESS_FAULT:  return "Load access fault";
    case EXC_STORE_MISALIGNED:   return "Store address misaligned";
    case EXC_STORE_ACCESS_FAULT: return "Store access fault";
    case EXC_ECALL_U:            return "Environment call from U-mode";
    case EXC_ECALL_S:            return "Environment call from S-mode";
    case EXC_ECALL_M:            return "Environment call from M-mode";
    case EXC_INST_PAGE_FAULT:    return "Instruction page fault";
    case EXC_LOAD_PAGE_FAULT:    return "Load page fault";
    case EXC_STORE_PAGE_FAULT:   return "Store page fault";
    default:                     return "Unknown exception";
    }
}

// Convert interrupt code to string
static const char *interrupt_to_string(uint64_t code) {
    switch (code) {
    case INT_SOFTWARE_S:  return "Supervisor software interrupt";
    case INT_SOFTWARE_M:  return "Machine software interrupt";
    case INT_TIMER_S:     return "Supervisor timer interrupt";
    case INT_TIMER_M:     return "Machine timer interrupt";
    case INT_EXTERNAL_S:  return "Supervisor external interrupt";
    case INT_EXTERNAL_M:  return "Machine external interrupt";
    default:              return "Unknown interrupt";
    }
}

// C trap handler called from trap.S
void riscv_trap_handler(arch_context_t *context, uint64_t cause, uint64_t tval) {
    uint64_t code = CAUSE_EXCEPTION_CODE(cause);
    int is_interrupt = (cause & CAUSE_INTERRUPT_BIT) != 0;
    
    if (is_interrupt) {
        // Handle interrupt through INTC
        if (intc_primary) {
            intc_handle_irq(code);
        } else {
            // Fallback for early boot before INTC is initialized
            uart_puts("[RISC-V] Early interrupt (INTC not ready): ");
            uart_puts(interrupt_to_string(code));
            uart_puts("\n");
        }
    } else {
        // Handle exception
        uart_puts("\n[RISC-V] FATAL EXCEPTION\n");
        uart_puts("Exception: ");
        uart_puts(exception_to_string(code));
        uart_puts("\n");
        
        // Print trap value (address for memory faults, instruction for illegal inst)
        uart_puts("Trap value: 0x");
        for (int i = 60; i >= 0; i -= 4) {
            int digit = (tval >> i) & 0xF;
            uart_putc(digit < 10 ? '0' + digit : 'A' + digit - 10);
        }
        uart_puts("\n");
        
        // Print saved PC
        uart_puts("PC: 0x");
        for (int i = 60; i >= 0; i -= 4) {
            int digit = (context->sepc >> i) & 0xF;
            uart_putc(digit < 10 ? '0' + digit : 'A' + digit - 10);
        }
        uart_puts("\n");
        
        // Panic for now - proper exception handling would go here
        panic("Unhandled RISC-V exception");
    }
}

// Install exception handlers
void arch_install_exception_handlers(void) {
    // Set trap vector
    __asm__ volatile("csrw stvec, %0" : : "r"((uint64_t)trap_vector));
    
    // Clear pending interrupts
    __asm__ volatile("csrw sip, zero");
    
    // Disable all interrupts for now
    __asm__ volatile("csrw sie, zero");
}

// Get current privilege level
int arch_get_exception_level(void) {
    // RISC-V doesn't have exception levels like ARM
    // We could check the current privilege mode from sstatus
    // but for now just return 1 for supervisor mode
    return 1;
}