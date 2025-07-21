.section ".text.boot"

.global _start

_start:
    // U-Boot passes: x0=dtb, x1=0, x2=0, x3=0
    // Linux boot protocol expects: x0=dtb, x1=0, x2=0, x3=0
    // Save registers for U-Boot compatibility
    mov x19, x0  // Save DTB pointer
    mov x20, x1  // Should be 0
    mov x21, x2  // Should be 0
    mov x22, x3  // Should be 0

    // Get current exception level
    mrs x0, CurrentEL
    and x0, x0, #0xC
    cmp x0, #0x8
    beq el2_entry
    cmp x0, #0x4
    beq el1_entry

el3_entry:
    // Configure EL3 and drop to EL2
    // Set up SCR_EL3
    mov x0, #0x5b1    // RW=1, HCE=1, SMD=1, RES1=1, NS=1
    msr scr_el3, x0

    // Set up SPSR_EL3 to boot into EL2
    mov x0, #0x3c9    // EL2h with interrupts disabled
    msr spsr_el3, x0

    // Set ELR_EL3 to continue execution in EL2
    adr x0, el2_entry
    msr elr_el3, x0
    eret

el2_entry:
    // Configure EL2 and drop to EL1
    // Enable AArch64 in EL1
    mov x0, #(1 << 31)
    msr hcr_el2, x0

    // Set up SCTLR_EL1
    mov x0, #0x0
    msr sctlr_el1, x0

    // Set up SPSR_EL2 to boot into EL1
    mov x0, #0x3c5    // EL1h with interrupts disabled
    msr spsr_el2, x0

    // Set ELR_EL2 to continue execution in EL1
    adr x0, el1_entry
    msr elr_el2, x0
    eret

el1_entry:
    // Disable MMU and caches before setup
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 0)   // Clear M bit (MMU)
    bic x0, x0, #(1 << 2)   // Clear C bit (D-cache)
    bic x0, x0, #(1 << 12)  // Clear I bit (I-cache)
    msr sctlr_el1, x0
    isb

    // Invalidate caches and TLB
    ic iallu                // Invalidate all instruction caches
    tlbi vmalle1           // Invalidate all TLB entries
    dsb nsh
    isb

    // Set up stack pointer
    ldr x0, =_stack_top
    mov sp, x0

    // Clear BSS section
    ldr x0, =__bss_start
    ldr x1, =__bss_end
clear_bss:
    cmp x0, x1
    b.ge done_clear_bss
    str xzr, [x0], #8
    b clear_bss
done_clear_bss:

    // Jump to C code
    mov x0, x19       // Pass device tree pointer
    bl kernel_main

    // If kernel_main returns, hang
hang:
    wfe
    b hang

.section ".data"
.align 12
.global _stack_bottom
_stack_bottom:
    .skip 4096 * 4   // 16KB stack
.global _stack_top
_stack_top: