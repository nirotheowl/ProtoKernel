/*
 * arch/arm64/boot/boot.S
 * 
 * Author:  Michael W. Lloyd
 * Date: 2025-08-09
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

/* Import exception vectors from exceptions.S */
.extern exception_vectors

/* Early debug support removed - kernel uses proper UART after MMU enable */

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
 * ARM64 "Linux" boot header
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
    /* Boot sequence starts here */
    
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

    /* Get our actual physical load address dynamically 
     * The kernel is loaded at XX200000 (2MB offset within any aligned region)
     * We need to calculate the physical address by finding the difference
     * between where we are (physical) and where the linker thinks we are (virtual) */
.Lget_phys_addr:
    adr x28, .Lget_phys_addr  /* Get current physical address of this label */
    ldr x0, =.Lget_phys_addr  /* Get virtual address of this label */
    sub x28, x28, x0          /* Calculate offset: phys - virt */
    ldr x0, =_start           /* Get virtual address of _start */
    add x28, x28, x0          /* Add offset to get physical address of _start */
    /* x28 now contains actual load address (e.g., 0x40200000 or 0x80200000) */
    
    /* Store physical base for later use */
    adr x0, phys_base_storage
    str x28, [x0]
    
    /* Physical base is now in x28 (e.g., 0x40200000 or 0x80200000) */
    
    /* Calculate the region we will identity map (needed for DTB check)
     * We align down to 64MB boundary to ensure we map a large enough region */
    mov x15, x28                          /* Get kernel physical base */
    and x15, x15, #~0x3FFFFFF            /* Align down to 64MB boundary */
    /* x15 = start of region to map (e.g., 0x40000000 for 0x40200000) */
    
    /*
     * Copy DTB to a safe location if needed
     * U-Boot may place DTB outside our identity-mapped region
     * We'll copy it to a location after our kernel that will be mapped
     */
    mov x0, x27                           /* x0 = DTB pointer from x27 */
    cbz x0, .Lno_dtb                     /* Skip if no DTB */
    
    /* Check if DTB is within our identity-mapped region (x15 to x15+128MB) */
    mov x1, x15                           /* Get start of mapped region */
    cmp x0, x1
    b.lo .Ldtb_needs_copy                /* DTB below mapped region */
    mov x1, #128                          /* 128MB */
    lsl x1, x1, #20                       /* Shift left by 20 to get 128MB */
    add x1, x15, x1                       /* Add to get end of region */
    cmp x0, x1
    b.hs .Ldtb_needs_copy                /* DTB above mapped region */
    b .Ldtb_ok                           /* DTB is in mapped region */
    
.Ldtb_needs_copy:
    
    /* DTB needs to be copied - place it after kernel end */
    adr x1, _kernel_end                   /* Get physical address of kernel end */
    add x1, x1, #0x10000                  /* Add 64KB offset for safety */
    and x1, x1, #~0xFFF                   /* Align to page boundary */
    
    /* Copy DTB header first to get size */
    ldr w2, [x0, #4]                      /* Load totalsize field (offset 4) */
    rev w2, w2                            /* Convert from big-endian */
    
    /* Copy DTB data */
    mov x3, x0                            /* Source */
    mov x4, x1                            /* Destination */
    mov x5, x2                            /* Size */
.Lcopy_dtb_loop:
    cbz x5, .Lcopy_dtb_done
    ldrb w6, [x3], #1
    strb w6, [x4], #1
    sub x5, x5, #1
    b .Lcopy_dtb_loop
.Lcopy_dtb_done:
    mov x27, x1                           /* Update DTB pointer to new location */
    
.Ldtb_ok:
.Lno_dtb:


    /*
     * IMPORTANT: We're currently running at physical addresses with MMU off.
     * The symbols from the linker script are virtual addresses, so we can't
     * use them directly yet. We need to:
     * 1. Set up a physical stack
     * 2. Clear BSS using physical addresses
     * 3. Only use virtual addresses after MMU is enabled
     */

    /*
     * Clear BSS Section
     * =================
     * Clear the BSS section using physical addresses
     */
    
    /* Get BSS start and end addresses (virtual) and convert to physical */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    ldr x2, =KERNEL_VIRT_BASE
    sub x0, x0, x2          /* Convert virtual to offset */
    sub x1, x1, x2          /* Convert virtual to offset */
    add x0, x0, x28         /* Add physical base to get physical BSS start */
    add x1, x1, x28         /* Add physical base to get physical BSS end */
    
    /* Clear BSS */
1:  cmp x0, x1
    b.hs 2f                 /* If start >= end, we're done */
    str xzr, [x0], #8       /* Store zero and increment pointer by 8 */
    b 1b
2:

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
    mov x1, #(12 * PAGE_SIZE)             /* Size to clear (12 pages) */
    bl func_zero_memory                   /* Call helper function */

    /* Set up page table pointers */
    mov x25, x26                          /* x25 = current free page */
    
    /* TTBR0 L0 table */
    mov x23, x25                          /* x23 = L0 table for TTBR0 */
    add x25, x25, #PAGE_SIZE
    
    /* TTBR0 L1 table */
    mov x22, x25                          /* x22 = L1 table for TTBR0 */
    add x25, x25, #PAGE_SIZE
    
    /* TTBR0 L2 table - for now just allocate one table
     * TODO: Properly handle multiple GB regions */
    mov x21, x25                          /* x21 = L2 table for TTBR0 */
    add x25, x25, #PAGE_SIZE

    /* Create identity mapping using 2MB blocks.
     * Map 128MB region around where kernel is loaded.
     * This handles various load addresses and memory configurations. */
    
    /* x15 already contains the start of region to map (calculated earlier) */
    
    /* Calculate L0 index (bits 47:39) - will be 0 for addresses < 512GB */
    lsr x14, x15, #L0_SHIFT
    and x14, x14, #0x1FF                  /* Mask to 9 bits */
    
    /* Calculate L1 index (bits 38:30) - which GB region */
    lsr x13, x15, #L1_SHIFT  
    and x13, x13, #0x1FF                  /* Mask to 9 bits */
    
    /* Step 1: L0 entry pointing to L1 */
    mov x0, x22                           /* L1 table physical address */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    lsl x1, x14, #3                       /* L0 index * 8 */
    str x0, [x23, x1]                     /* Store in L0[index] */
    
    /* Step 2: L1 entry pointing to L2 table */
    /* TODO: This currently only works for single GB mappings.
     * For server platform at 0x80200000 (GB 2), we need to allocate
     * separate L2 tables for each GB */
    mov x0, x21                           /* Use L2 table */
    mov x2, x0                            /* Save L2 table base for later use */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    lsl x1, x13, #3                       /* L1 index * 8 */
    str x0, [x22, x1]                     /* Store in L1[index] */
    
    /* Step 3: L2 entries as 2MB blocks */
    /* Map 64 * 2MB = 128MB starting from aligned base */
    mov x0, x15                           /* Physical address to map */
    mov x1, #ATTR_AF | ATTR_SH_INNER | ATTR_MAIR_IDX_NORMAL | ATTR_AP_RW_EL1
    orr x1, x1, #BLOCK_DESC               /* Block descriptor */
    /* x2 already contains L2 table base */
    mov x3, #64                           /* Number of 2MB blocks (128MB total) */
    
1:  orr x4, x0, x1                        /* Combine address and attributes */
    str x4, [x2], #8                      /* Store and increment pointer */
    add x0, x0, #L2_BLOCK_SIZE            /* Next 2MB block */
    subs x3, x3, #1                       /* Decrement counter */
    b.ne 1b

    /*
     * Create Higher-Half Mapping Page Tables (TTBR1)
     * ==============================================
     * Map kernel from its physical load address to higher virtual addresses
     */
    
    /* Allocate TTBR1 page tables (continuing from x25)
     * These are for the kernel's virtual address space */
    mov x19, x25                          /* x19 = L0 table for TTBR1 */
    add x25, x25, #PAGE_SIZE
    
    mov x18, x25                          /* x18 = L1 table for TTBR1 */
    add x25, x25, #PAGE_SIZE
    
    mov x17, x25                          /* x17 = L2 table for TTBR1 kernel */
    add x25, x25, #PAGE_SIZE
    
    mov x16, x25                          /* x16 = L2 table for TTBR1 devices */
    add x25, x25, #PAGE_SIZE
    
    /* Create TTBR1 higher-half mapping
     * With T1SZ=16 (48-bit VA), top 16 bits (63:48) must be all 1s for TTBR1.
     * The page table walk uses bits [47:0] for indexing. */
    
    /* Step 1: L0 entry at index 0x000 pointing to L1 */
    mov x0, x18                           /* L1 table physical address */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    str x0, [x19]                         /* Store in L0[0] - NOT L0[0x1FE]! */
    
    /* Step 2a: L1 entry at index 0 pointing to device L2 */
    mov x0, x16                           /* L2 table physical address for devices */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    str x0, [x18]                         /* Store in L1[0] (index 0) */
    
    /* Step 2b: L1 entry pointing to kernel L2
     * Virtual address is always fixed at 0xFFFF000040200000
     * This maps to L1 index 1 (second GB of virtual space)
     * We keep this simple - virtual address stays the same regardless of physical */
    mov x0, x17                           /* L2 table physical address for kernel */
    orr x0, x0, #TABLE_DESC               /* Table descriptor */
    str x0, [x18, #8]                     /* Store in L1[1] - fixed for VA 0xFFFF000040000000 */
    
    /* Step 3: L2 entries for kernel mapping
     * Map from where we're actually loaded (x28 contains actual load address).
     * The kernel is loaded at XX200000 (2MB offset within any aligned region).
     * 
     * Virtual mapping is always fixed at 0xFFFF000040200000 regardless of physical.
     * This gives us position-independent code at a known virtual address.
     */
    
    /* Fixed L2 index for virtual address 0xFFFF000040200000 
     * This is index 1 within the L2 table (0x200000 / 0x200000 = 1) */
    mov x12, #1                           
    
    /* Calculate starting position in L2 table */
    mov x2, x17                           /* L2 table base */
    lsl x11, x12, #3                      /* L2 index * 8 bytes per entry */
    add x2, x2, x11                       /* Point to correct L2 entry */
    
    /* Map from the actual physical load address 
     * x28 contains the actual physical load address (e.g., 0x40200000, 0x80200000)
     * We need to align it down to 2MB for the block descriptor */
    mov x0, x28                           /* Get actual load address */
    mov x1, #0x1FFFFF                     /* Load 2MB - 1 mask */
    bic x0, x0, x1                        /* Align down to 2MB boundary for block mapping */
    mov x1, #ATTR_AF | ATTR_SH_INNER | ATTR_MAIR_IDX_NORMAL | ATTR_AP_RW_EL1
    orr x1, x1, #BLOCK_DESC               /* Block descriptor */
    mov x3, #64                           /* 64 * 2MB = 128MB */
    
3:  orr x4, x0, x1                        /* Combine address and attributes */
    str x4, [x2], #8                      /* Store and increment pointer */
    add x0, x0, #L2_BLOCK_SIZE            /* Next 2MB physical block */
    subs x3, x3, #1                       /* Decrement counter */
    b.ne 3b

    /* Configure MAIR_EL1 (Memory Attribute Indirection Register)
     * MAIR_EL1 defines memory types for use in page table entries:
     * Index 0: Normal memory, Inner/Outer Write-Back Non-transient
     * Index 1: Device memory, nGnRnE */
    mov x0, #0x00FF                       /* Index 0: Normal memory, Index 1: Device */
    msr mair_el1, x0

    /* Configure TCR_EL1 (Translation Control Register)
     * Controls address translation parameters:
     * - IPS: Intermediate Physical Address Size (2 = 40-bit, supports up to 1TB)
     * - T0SZ/T1SZ: Size of address space (16 = 48-bit)
     * - TG0/TG1: Translation granule (0/2 = 4KB)
     * - SH0/SH1: Shareability (3 = Inner shareable)
     * - ORGN0/ORGN1: Outer cacheability (1 = Write-back, write-allocate)
     * - IRGN0/IRGN1: Inner cacheability (1 = Write-back, write-allocate)
     * - TBI0: Top byte ignored for tagging */
    mov x0, #16                           /* T0SZ = 16 */
    orr x0, x0, #(16 << 16)               /* T1SZ = 16 */
    mov x1, #0x5                          /* IPS = 5 (48-bit physical addresses) */
    lsl x1, x1, #32                       /* Shift to correct position */
    orr x0, x0, x1                        /* Set IPS field */
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

    /* Initialize MMU page tables */

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

    /* MMU is now enabled, running at virtual addresses */

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
    
    /* Set up stack pointer using the pre-allocated stack in .data section
     * This avoids assumptions about memory layout */
    ldr x0, =_stack_top                   /* Use the stack defined in .data */
    mov sp, x0

    /* Pass DTB pointer to init_arm64 */
    mov x0, x27                           /* Restore DTB pointer from x27 */
    bl init_arm64

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

/* Storage for dynamically detected physical base address */
.align 3
.global phys_base_storage
phys_base_storage:
    .quad 0

