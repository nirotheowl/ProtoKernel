/*
 * arch/arm64/boot.S
 * 
 * Author:  Michael W. Lloyd
 * Date: 2025-07-25
 * 
 * Description: 64-bit ARM kernel bootstrap code implementing higher-half
 *              virtual memory with MMU setup, page table creation, and
 *              transition from physical to virtual address space.
 */

.section ".text.boot"

.global _start

/* Import constants from linker script */
.extern KERNEL_VIRT_BASE
.extern KERNEL_PHYS_BASE
.extern _kernel_end

/*
 * ARMv8 Page Table Configuration
 * ==============================
 * 
 * Page Table Strategy:
 * - TTBR0: Identity mapping for bootstrap (PA = VA)
 * - TTBR1: Kernel mapping in higher virtual addresses
 * 
 * Page Table Hierarchy (4KB pages, 48-bit VA):
 * - L0: 512 entries, each covers 512GB
 * - L1: 512 entries, each covers 1GB  
 * - L2: 512 entries, each covers 2MB
 * - L3: 512 entries, each covers 4KB (if used)
 */

/*
 * ARM64 Linux-compatible Image Header
 * Required for bootloader compatibility (U-Boot, QEMU, etc.)
 */
_start:
    /* Executable code - jump over the header to actual code */
    b       _boot_start              /* code0: branch to actual code */
    .long   0                        /* code1: reserved */
    .quad   0x0                      /* text_offset: kernel load offset (0 = 2MB aligned) */
    .quad   _end - _start            /* image_size: total image size */
    .quad   0x8                      /* flags: LE, 4K pages, bit 3 = anywhere in RAM */
    .quad   0                        /* res2: reserved */
    .quad   0                        /* res3: reserved */  
    .quad   0                        /* res4: reserved */
    .long   0x644d5241               /* magic: "ARM\x64" (LE) */
    .long   0                        /* res5: reserved */

/* Ensure we start at offset 64 (0x40) from _start */
.org 0x40
_boot_start:
    /* U-Boot passes: x0=dtb, x1=0, x2=0, x3=0
     * Linux boot protocol expects: x0=dtb, x1=0, x2=0, x3=0
     * Save registers for U-Boot compatibility */
    mov x27, x0  /* Save DTB pointer in x27 (we'll use x19 for page tables) */
    mov x20, x1  /* Should be 0 */
    mov x21, x2  /* Should be 0 */
    mov x22, x3  /* Should be 0 */

    /* Get current exception level */
    mrs x0, CurrentEL
    and x0, x0, #0xC
    cmp x0, #0x8
    beq el2_entry
    cmp x0, #0x4
    beq el1_entry

el3_entry:
    /* Configure EL3 and drop to EL2 */
    /* Set up SCR_EL3 */
    mov x0, #0x5b1    /* RW=1, HCE=1, SMD=1, RES1=1, NS=1 */
    msr scr_el3, x0

    /* Set up SPSR_EL3 to boot into EL2 */
    mov x0, #0x3c9    /* EL2h with interrupts disabled */
    msr spsr_el3, x0

    /* Set ELR_EL3 to continue execution in EL2 */
    adr x0, el2_entry
    msr elr_el3, x0
    eret

el2_entry:
    /* Configure EL2 and drop to EL1 */
    /* Enable AArch64 in EL1 */
    mov x0, #(1 << 31)
    msr hcr_el2, x0

    /* Set up SCTLR_EL1 */
    mov x0, #0x0
    msr sctlr_el1, x0

    /* Set up SPSR_EL2 to boot into EL1 */
    mov x0, #0x3c5    /* EL1h with interrupts disabled */
    msr spsr_el2, x0

    /* Set ELR_EL2 to continue execution in EL1 */
    adr x0, el1_entry
    msr elr_el2, x0
    eret

el1_entry:
    /* Disable MMU and caches before setup */
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 0)   /* Clear M bit (MMU) */
    bic x0, x0, #(1 << 2)   /* Clear C bit (D-cache) */
    bic x0, x0, #(1 << 12)  /* Clear I bit (I-cache) */
    msr sctlr_el1, x0
    isb

    /* Invalidate caches and TLB */
    ic iallu                /* Invalidate all instruction caches */
    tlbi vmalle1           /* Invalidate all TLB entries */
    dsb nsh
    isb

    
    /* Get our actual physical load address */
    adr x28, _start         /* Get PC-relative address (physical) */
    ldr x29, =_start        /* Get absolute address from linker (virtual) */
    ldr x30, =KERNEL_VIRT_BASE
    sub x29, x29, x30       /* Get offset from virtual base */
    sub x28, x28, x29       /* Calculate physical base address */
    /* Now x28 = physical address where kernel is loaded */


    /*
     * IMPORTANT: We're currently running at physical addresses with MMU off.
     * The symbols from the linker script are virtual addresses, so we can't
     * use them directly yet. We need to:
     * 1. Set up a physical stack
     * 2. Clear BSS using physical addresses
     * 3. Only use virtual addresses after MMU is enabled
     */

    /*
     * Create Identity Mapping Page Tables
     * ===================================
     * We'll create a simple identity mapping for the bootstrap phase.
     * This maps physical addresses to themselves in TTBR0.
     */

    /* Page table entry bits for ARMv8 MMU */
    .equ ATTR_MAIR_IDX_NORMAL, 0x0 << 2  /* MAIR index 0 (normal memory) */
    .equ ATTR_MAIR_IDX_DEVICE, 0x1 << 2  /* MAIR index 1 (device memory) */
    .equ ATTR_AP_RW_EL1, 0x0 << 6        /* Read-write at EL1 */
    .equ ATTR_AP_RO_EL1, 0x2 << 6        /* Read-only at EL1 */
    .equ ATTR_SH_INNER, 0x3 << 8         /* Inner shareable */
    .equ ATTR_AF, 0x1 << 10               /* Access flag */
    .equ ATTR_nG, 0x1 << 11               /* Not global */
    .equ ATTR_UXN, 0x1 << 54              /* Unprivileged execute never */
    .equ ATTR_PXN, 0x1 << 53              /* Privileged execute never */

    /* Entry types */
    .equ TABLE_DESC, 0x3                  /* Table descriptor */
    .equ BLOCK_DESC, 0x1                  /* Block descriptor */
    .equ PAGE_DESC, 0x3                   /* Page descriptor (L3 only) */

    /* Page sizes */
    .equ PAGE_SIZE, 4096
    .equ PAGE_SHIFT, 12
    .equ TABLE_SHIFT, 9                   /* 512 entries per table */
    .equ TABLE_ENTRIES, 512

    /* Level shifts for 4KB pages with 48-bit VA */
    .equ L0_SHIFT, 39                     /* Bits 47:39 */
    .equ L1_SHIFT, 30                     /* Bits 38:30 */
    .equ L2_SHIFT, 21                     /* Bits 29:21 */
    .equ L3_SHIFT, 12                     /* Bits 20:12 */

    /* Block/page sizes at each level */
    .equ L1_BLOCK_SIZE, 0x40000000        /* 1GB */
    .equ L2_BLOCK_SIZE, 0x200000          /* 2MB */
    .equ L3_PAGE_SIZE, 0x1000             /* 4KB */

    /* Calculate page table addresses
     * We'll place page tables after kernel end for U-Boot compatibility.
     * First, we need to calculate where kernel ends in physical memory. */
    adr x26, _kernel_end                  /* Get physical address of kernel end */
    add x26, x26, #0x1000                 /* Add 4KB padding for safety */
    and x26, x26, #~0xFFF                 /* Align to page boundary (4KB) */
    
    /* Clear page table memory first */
    mov x0, x26                           /* Start address */
    mov x1, #(9 * PAGE_SIZE)              /* Size to clear (9 pages total) */
    bl func_zero_memory                   /* Call helper function */

    /* Set up page table pointers */
    mov x25, x26                          /* x25 = current free page */
    
    /* TTBR0 L0 table */
    mov x23, x25                          /* x23 = L0 table for TTBR0 */
    add x25, x25, #PAGE_SIZE
    
    /* TTBR0 L1 table */
    mov x22, x25                          /* x22 = L1 table for TTBR0 */
    add x25, x25, #PAGE_SIZE
    
    /* TTBR0 L2 table for first 512MB */
    mov x21, x25                          /* x21 = L2 table for TTBR0 */
    add x25, x25, #PAGE_SIZE
    
    /* TTBR0 L2 table for second 512MB (where kernel is) */
    mov x20, x25                          /* x20 = L2 table for second 512MB */
    add x25, x25, #PAGE_SIZE

    /* Create identity mapping using 2MB blocks.
     * We'll map the first 1GB of physical memory. */
    
    /* Step 1: L0 entry pointing to L1 */
    mov x0, x22                           /* L1 table physical address */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    str x0, [x23]                         /* Store in L0[0] */
    
    /* Step 2: L1 entries pointing to L2 tables */
    /* L1[0] for 0-512MB */
    mov x0, x21                           /* L2 table physical address */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    str x0, [x22]                         /* Store in L1[0] */
    
    /* L1[1] for 512MB-1GB (where kernel is) */
    mov x0, x20                           /* L2 table physical address */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    str x0, [x22, #8]                     /* Store in L1[1] */
    
    /* Step 3: L2 entries as 2MB blocks */
    /* First map 256 * 2MB = 512MB starting from physical address 0 */
    mov x0, #0                            /* Physical address */
    mov x1, #ATTR_AF | ATTR_SH_INNER | ATTR_MAIR_IDX_NORMAL | ATTR_AP_RW_EL1
    orr x1, x1, #BLOCK_DESC               /* Block descriptor */
    mov x2, x21                           /* L2 table base */
    mov x3, #256                          /* Number of 2MB blocks */
    
1:  orr x4, x0, x1                        /* Combine address and attributes */
    str x4, [x2], #8                      /* Store and increment pointer */
    add x0, x0, #L2_BLOCK_SIZE            /* Next 2MB block */
    subs x3, x3, #1                       /* Decrement counter */
    b.ne 1b
    
    /* Now map second 512MB */
    /* L1[1] covers second 512MB region */
    mov x0, #0x40000000                   /* Start at 1GB */
    mov x2, x20                           /* L2 table base for second 512MB */
    mov x3, #256                          /* Number of 2MB blocks */
    
2:  orr x4, x0, x1                        /* Combine address and attributes */
    str x4, [x2], #8                      /* Store and increment pointer */
    add x0, x0, #L2_BLOCK_SIZE            /* Next 2MB block */
    subs x3, x3, #1                       /* Decrement counter */
    b.ne 2b

    /*
     * Create Higher-Half Mapping Page Tables (TTBR1)
     * ==============================================
     * Map kernel from its physical load address to higher virtual addresses
     */
    
    /* Allocate TTBR1 page tables (continuing from x25) */
    mov x19, x25                          /* x19 = L0 table for TTBR1 */
    add x25, x25, #PAGE_SIZE
    
    mov x18, x25                          /* x18 = L1 table for TTBR1 */
    add x25, x25, #PAGE_SIZE
    
    mov x17, x25                          /* x17 = L2 table for TTBR1 */
    add x25, x25, #PAGE_SIZE
    
    /* Create TTBR1 higher-half mapping
     * With T1SZ=16 (48-bit VA), top 16 bits (63:48) must be all 1s for TTBR1.
     * The page table walk uses bits [47:0] for indexing. */
    
    /* Step 1: L0 entry at index 0x000 pointing to L1 */
    mov x0, x18                           /* L1 table physical address */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    str x0, [x19]                         /* Store in L0[0] - NOT L0[0x1FE]! */
    
    /* Step 2: L1 entry at index 1 pointing to L2 */
    mov x0, x17                           /* L2 table physical address */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    str x0, [x18, #8]                     /* Store in L1[1] (index 1) */
    
    /* Step 3: L2 entries for kernel mapping
     * Map from where we're loaded (x28 contains physical base).
     * Using 2MB blocks, aligned to 2MB boundary.
     * All L2 entries point to same physical region (fixed mapping). */
    mov x0, x28                           /* Restore physical base from x28 */
    mov x1, #L2_BLOCK_SIZE - 1            /* Mask for 2MB alignment (0x1FFFFF) */
    bic x0, x0, x1                        /* Clear lower 21 bits to align to 2MB */
    mov x1, #ATTR_AF | ATTR_SH_INNER | ATTR_MAIR_IDX_NORMAL | ATTR_AP_RW_EL1
    orr x1, x1, #BLOCK_DESC               /* Block descriptor */
    mov x2, x17                           /* L2 table base */
    mov x3, #32                           /* 32 * 2MB = 64MB */
    
3:  orr x4, x0, x1                        /* Combine address and attributes */
    str x4, [x2], #8                      /* Store and increment pointer */
    /* Don't increment physical address - all entries map to same region */
    subs x3, x3, #1                       /* Decrement counter */
    b.ne 3b

    /* Configure MAIR_EL1 (Memory Attribute Indirection Register)
     * MAIR_EL1 defines memory types for use in page table entries:
     * Index 0: Normal memory, Inner/Outer Write-Back Non-transient
     * Index 1: Device memory, nGnRnE */
    mov x0, #0xFF                         /* Normal memory attributes */
    msr mair_el1, x0

    /* Configure TCR_EL1 (Translation Control Register)
     * Controls address translation parameters:
     * - T0SZ/T1SZ: Size of address space (16 = 48-bit)
     * - TG0/TG1: Translation granule (0/2 = 4KB)
     * - SH0/SH1: Shareability (3 = Inner shareable)
     * - ORGN0/ORGN1: Outer cacheability (1 = Write-back, write-allocate)
     * - IRGN0/IRGN1: Inner cacheability (1 = Write-back, write-allocate)
     * - TBI0: Top byte ignored for tagging */
    mov x0, #16                           /* T0SZ = 16 */
    orr x0, x0, #(16 << 16)               /* T1SZ = 16 */
    orr x0, x0, #(0x3 << 12)              /* SH0 = Inner shareable */
    orr x0, x0, #(0x3 << 28)              /* SH1 = Inner shareable */
    orr x0, x0, #(0x1 << 10)              /* ORGN0 = WBWA */
    orr x0, x0, #(0x1 << 26)              /* ORGN1 = WBWA */
    orr x0, x0, #(0x1 << 8)               /* IRGN0 = WBWA */
    orr x0, x0, #(0x1 << 24)              /* IRGN1 = WBWA */
    orr x0, x0, #(0x2 << 30)              /* TG1 = 4KB */
    mov x1, #(0x1 << 37)                  /* TBI0 = 1 (top byte ignored) */
    orr x0, x0, x1
    msr tcr_el1, x0

    /* Set TTBR0_EL1 to our L0 table (identity mapping) */
    msr ttbr0_el1, x23
    
    /* Set TTBR1_EL1 to our L0 table (higher-half mapping) */
    msr ttbr1_el1, x19
    isb
    
    /* Force a TLB invalidation to ensure new mappings are visible */
    tlbi vmalle1
    dsb ish
    isb

    /* Set up exception vectors before enabling MMU */
    adr x0, exception_vectors
    msr vbar_el1, x0
    isb

    /* Enable MMU in SCTLR_EL1
     * M bit (0): MMU enable
     * C bit (2): D-cache enable
     * I bit (12): I-cache enable */
    mrs x0, sctlr_el1
    orr x0, x0, #(1 << 0)                 /* M bit (MMU enable) */
    orr x0, x0, #(1 << 2)                 /* C bit (D-cache enable) */
    orr x0, x0, #(1 << 12)                /* I bit (I-cache enable) */
    msr sctlr_el1, x0
    isb

    /* We're now running with identity mapping!
     * Time to jump to higher half addresses. */
    
    /* Jump to higher half virtual address
     * Load the virtual address of higher_half label */
    adr x1, .Lhigher_half_addr        /* Get address of constant */
    ldr x1, [x1]                      /* Load 64-bit virtual address */
    br x1                             /* Jump to virtual address */
    
    /* Store the virtual address as a 64-bit constant */
    .align 3
.Lhigher_half_addr:
    .quad higher_half

higher_half:
    /* We're now running at virtual addresses in the higher half! */
    
    /* Set up stack pointer using virtual address */
    ldr x0, =_start
    sub x0, x0, #0x10000                  /* 64KB stack below code */
    mov sp, x0

    /* Pass DTB pointer to kernel_main */
    mov x0, x27                           /* Restore DTB pointer from x27 */
    bl kernel_main

    /* If kernel_main returns, hang */
hang:
    wfe
    b hang

/* Helper function to zero memory
 * x0 = start address, x1 = size in bytes */
func_zero_memory:
    cbz x1, 2f
1:  str xzr, [x0], #8
    sub x1, x1, #8
    cbnz x1, 1b
2:  ret


.section ".data"
.align 12
.global _stack_bottom
_stack_bottom:
    .skip 4096 * 4   /* 16KB stack */
.global _stack_top
_stack_top:

