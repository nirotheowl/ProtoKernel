/*
 * arch/riscv/boot/boot.S
 * 
 * Author:  Michael W. Lloyd
 * Date: 2025-08-11
 * 
 * Description: RISC-V kernel bootstrap code implementing higher-half
 *              virtual memory with MMU setup, page table creation, and
 *              transition from physical to virtual address space.
 *              
 *              Assumes Sv39 MMU mode with 3-level page tables.
 */

.section ".text.boot"

.global _start

/* Import constants from linker script */
.extern KERNEL_VIRT_BASE
.extern KERNEL_PHYS_BASE
.extern _kernel_end

/* Import trap handler from trap.S */
.extern trap_vector

/*
 * RISC-V Sv39 Page Table Configuration
 * =====================================
 * 
 * Page Table Strategy:
 * - Single SATP register points to root page table
 * - Identity mapping for bootstrap (PA = VA)
 * - Kernel mapping in higher virtual addresses
 * 
 * Sv39 Page Table Hierarchy (4KB pages, 39-bit VA):
 * - L2 (PGD): 512 entries, each covers 1GB
 * - L1 (PMD): 512 entries, each covers 2MB  
 * - L0 (PTE): 512 entries, each covers 4KB
 * 
 * Virtual Address Layout (39 bits):
 * [38:30] L2 index | [29:21] L1 index | [20:12] L0 index | [11:0] offset
 */

/*
 * RISC-V CSR (Control and Status Register) Definitions
 * =====================================================
 * 
 * Key registers used during boot:
 * 
 * mstatus/sstatus - Machine/Supervisor Status Register
 *   MPP[12:11]  - Previous privilege mode (M-mode)
 *   SPP[8]      - Previous privilege mode (S-mode)
 *   MPIE[7]     - Previous interrupt enable (M-mode)
 *   SPIE[5]     - Previous interrupt enable (S-mode)
 *   MIE[3]      - Global interrupt enable (M-mode)
 *   SIE[1]      - Global interrupt enable (S-mode)
 *   FS[14:13]   - FPU status (00=off, 01=initial, 10=clean, 11=dirty)
 * 
 * satp - Supervisor Address Translation and Protection
 *   MODE[63:60] - Translation mode (0=bare, 8=Sv39)
 *   ASID[59:44] - Address space identifier (16 bits)
 *   PPN[43:0]   - Physical page number of root page table (44 bits)
 * 
 * medeleg/mideleg - Machine Exception/Interrupt Delegation
 *   Each bit represents an exception/interrupt to delegate to S-mode
 * 
 * stvec - Supervisor Trap Vector
 *   BASE[63:2]  - Vector base address (4-byte aligned)
 *   MODE[1:0]   - Vector mode (0=direct, 1=vectored)
 */

/*
 * RISC-V Sv39 Page Table Entry (PTE) Format
 * ==========================================
 * 
 * PTE Layout (64 bits):
 * [63:54] Reserved (must be 0)
 * [53:28] PPN[2] - Physical page number part 2 (26 bits)
 * [27:19] PPN[1] - Physical page number part 1 (9 bits)
 * [18:10] PPN[0] - Physical page number part 0 (9 bits)
 * [9:8]   RSW    - Reserved for software (2 bits)
 * [7]     D      - Dirty (page has been written)
 * [6]     A      - Accessed (page has been accessed)
 * [5]     G      - Global (mapping exists in all address spaces)
 * [4]     U      - User (accessible in U-mode)
 * [3]     X      - Execute permission
 * [2]     W      - Write permission
 * [1]     R      - Read permission
 * [0]     V      - Valid
 * 
 * Page Types (determined by R,W,X bits):
 * - 000: Pointer to next level page table (non-leaf)
 * - 001: Read-only page
 * - 010: Reserved (invalid)
 * - 011: Read-write page
 * - 100: Execute-only page
 * - 101: Read-execute page
 * - 110: Reserved (invalid)
 * - 111: Read-write-execute page
 * 
 * Page Sizes in Sv39:
 * - Level 2 PTE: 1GB megapage (if R|W|X != 0)
 * - Level 1 PTE: 2MB megapage (if R|W|X != 0)
 * - Level 0 PTE: 4KB page
 */

/* PTE attribute bits for RISC-V Sv39 */
.equ PTE_V,     0x001    /* Valid */
.equ PTE_R,     0x002    /* Read permission */
.equ PTE_W,     0x004    /* Write permission */
.equ PTE_X,     0x008    /* Execute permission */
.equ PTE_U,     0x010    /* User accessible */
.equ PTE_G,     0x020    /* Global mapping */
.equ PTE_A,     0x040    /* Accessed */
.equ PTE_D,     0x080    /* Dirty */

/* Common PTE combinations */
.equ PTE_KERNEL_RX,  PTE_V | PTE_R | PTE_X | PTE_A | PTE_D | PTE_G
.equ PTE_KERNEL_RW,  PTE_V | PTE_R | PTE_W | PTE_A | PTE_D | PTE_G
.equ PTE_KERNEL_RO,  PTE_V | PTE_R | PTE_A | PTE_G
.equ PTE_TABLE,      PTE_V  /* Points to next level */

/* Page sizes */
.equ PAGE_SHIFT,     12
.equ PAGE_SIZE,      (1 << PAGE_SHIFT)      /* 4KB */
.equ MEGAPAGE_SHIFT, 21
.equ MEGAPAGE_SIZE,  (1 << MEGAPAGE_SHIFT)  /* 2MB */
.equ GIGAPAGE_SHIFT, 30
.equ GIGAPAGE_SIZE,  (1 << GIGAPAGE_SHIFT)  /* 1GB */

/* SATP mode values */
.equ SATP_MODE_BARE, 0
.equ SATP_MODE_SV39, 8
.equ SATP_MODE_SV48, 9

/* Privilege modes */
.equ PRV_U, 0    /* User mode */
.equ PRV_S, 1    /* Supervisor mode */
.equ PRV_M, 3    /* Machine mode */

/*
 * RISC-V Linux Boot Image Header
 * ===============================
 * Required for bootloader compatibility (U-Boot, OpenSBI, QEMU, etc.)
 * Based on Linux kernel RISC-V image header format
 */

_start:
    /* Jump over the header to actual boot code */
    j       _boot_start              /* offset 0x00: jump to actual code */
    .word   0                        /* offset 0x04: reserved */
    .dword  0x0                      /* offset 0x08: text_offset (0 = 2MB aligned) */
    .dword  _end - _start            /* offset 0x10: image_size */
    .dword  0x0                      /* offset 0x18: flags */
    .word   0x2                      /* offset 0x20: version (version 0.2) */
    .word   0                        /* offset 0x24: reserved */
    .dword  0                        /* offset 0x28: reserved */
    .ascii  "RISCV\0\0\0"            /* offset 0x30: magic "RISCV" */
    .dword  0                        /* offset 0x38: reserved */

/* Ensure we start actual code at offset 64 (0x40) from _start */
.align 6
_boot_start:
    /*
     * Step 1: Save boot parameters
     * Bootloader passes: a0=hartid, a1=dtb pointer
     * Save in callee-saved registers that won't be clobbered
     */
    mv s0, a0                   /* s0 = hartid */
    mv s1, a1                   /* s1 = device tree pointer */
    
    /*
     * Note: DTB relocation will be handled after we set up identity mapping
     * We need to know our physical address range first to determine if DTB
     * is in a safe location. For now, just preserve the pointer.
     */

    /*
     * Step 2: Privilege mode handling
     * 
     * Standard entry (_start) assumes S-mode (OpenSBI/UEFI)
     * For bare metal M-mode, see _start_mmode entry point below
     */
    
    /* We're in S-mode (entered via OpenSBI/firmware) */
    /* Continue with S-mode initialization */
    
    /*
     * Calculate and store physical base address
     * TODO: Calculate this dynamically like ARM64 does
     * TEMPORARY: Hardcoded to 0x80200000 for QEMU testing
     * This MUST be fixed for real hardware support
     */
    li t0, 0x80200000          /* TEMPORARY - hardcoded for QEMU */
    la t1, phys_base_storage
    sd t0, 0(t1)

.Lcommon_smode_path:
    /* Common code for both S-mode and M-mode entry paths */
    /* At this point we're in S-mode with boot params saved */
    
    /*
     * Step 3: Set up early stack
     * Load the physical address of our bootstrap stack
     * Stack grows down, so we use the top address
     */
    la sp, boot_stack_top       /* Load stack top address */
    /* Stack is already 16-byte aligned from .align directive */

    /*
     * Step 4: Clear BSS section
     * Zero out all uninitialized global variables
     * We're still using physical addresses (MMU not enabled yet)
     */
    la t0, __bss_start          /* Load BSS start address */
    la t1, __bss_end            /* Load BSS end address */
    
.Lclear_bss_loop:
    beq t0, t1, .Lclear_bss_done   /* If start == end, we're done */
    sd zero, 0(t0)              /* Store zero at current address */
    addi t0, t0, 8              /* Move to next 8-byte word */
    j .Lclear_bss_loop          /* Continue loop */
    
.Lclear_bss_done:

    /*
     * Step 5: Set up identity mapping for bootstrap
     * TODO: Allocate page table pages (L2, L1, L0 as needed)
     * TODO: Create identity mapping for kernel code/data
     * TODO: Use 2MB pages (megapages) where possible for efficiency
     * TODO: Set appropriate PTE flags (V, R, W, X, A, D, G)
     * TODO: Check if DTB is within mapped region and relocate if necessary
     */

    /*
     * Step 6: Set up higher-half kernel mapping
     * TODO: Calculate virtual-to-physical offset
     * TODO: Create mapping from KERNEL_VIRT_BASE to KERNEL_PHYS_BASE
     * TODO: Map kernel text as R+X
     * TODO: Map kernel data as R+W
     * TODO: Map kernel rodata as R only
     */

    /*
     * Step 7: Configure and enable MMU
     * TODO: Set up SATP register with root page table address
     * TODO: Use Sv39 mode (MODE=8 in SATP)
     * TODO: Set ASID to 0 for kernel
     * TODO: Execute sfence.vma to flush TLB
     * TODO: Enable paging by writing SATP
     */

    /*
     * Step 8: Jump to higher-half addresses
     * TODO: Calculate jump target in virtual address space
     * TODO: Use indirect jump to transition to virtual addresses
     * TODO: Continue execution at higher_half label
     */

higher_half:
    /*
     * Step 9: Clean up identity mapping
     * TODO: Switch to virtual stack pointer
     * TODO: Remove identity mapping entries from page tables
     * TODO: Flush TLB with sfence.vma
     */

    /*
     * Step 10: Set up trap vector
     * TODO: Load address of trap_vector
     * TODO: Write to stvec register
     * TODO: Set MODE bits for vectored vs direct mode
     */

    /*
     * Step 11: Initialize CPU features
     * TODO: Enable FPU if present (set FS bits in sstatus)
     * TODO: Enable vector extensions if present
     * TODO: Set up performance counters if needed
     */

    /*
     * Step 12: Set up boot data for kernel
     * TODO: Prepare arguments for kernel_main
     * TODO: Pass hartid, device tree pointer, memory info
     * TODO: Ensure proper register allocation per RISC-V ABI
     */

    /*
     * Step 13: Jump to kernel main
     * TODO: Load address of kernel_main
     * TODO: Call kernel_main with proper arguments
     * TODO: Should never return, but handle if it does
     */

    /*
     * Step 14: Halt if kernel_main returns
     * TODO: Disable interrupts
     * TODO: Enter infinite wait loop with wfi instruction
     */

/*
 * Helper Functions
 * ================
 */

/*
 * create_page_table_entry:
 * TODO: Helper to create a PTE with given PA and flags
 * TODO: Handle different page sizes (4KB, 2MB, 1GB)
 * TODO: Set appropriate permission and attribute bits
 */

/*
 * map_page:
 * TODO: Helper to map a virtual page to physical page
 * TODO: Walk page table hierarchy
 * TODO: Allocate intermediate tables if needed
 * TODO: Install PTE at appropriate level
 */

/*
 * clear_page:
 * TODO: Helper to zero out a page
 * TODO: Use efficient sd instruction loop
 */

/*
 * flush_tlb:
 * TODO: Helper to flush TLB entries
 * TODO: Use sfence.vma instruction
 * TODO: Handle ASID if needed
 */

/*
 * Data Section
 * ============
 */

.section .data
.align 12  /* Align to page boundary (4KB) */

/*
 * boot_pgd:
 * TODO: Reserve space for L2 page table (root)
 * TODO: 512 entries * 8 bytes = 4KB
 */

/*
 * boot_pmd:
 * TODO: Reserve space for L1 page tables
 * TODO: May need multiple depending on memory layout
 */

/*
 * boot_pte:
 * TODO: Reserve space for L0 page tables if using 4KB pages
 * TODO: May not be needed if using 2MB megapages
 */

/* Bootstrap stack for early initialization */
.align 4  /* Align to 16 bytes for RISC-V ABI */
boot_stack_bottom:
    .space 0x4000  /* 16KB stack */
.global boot_stack_top
boot_stack_top:

/* Storage for dynamically detected physical base address */
.align 3
.global phys_base_storage
phys_base_storage:
    .dword 0

/*
 * Alternative entry point for M-mode (bare metal)
 * This entry point is for systems that boot directly in M-mode
 * without OpenSBI or other firmware.
 */
.section ".text.boot"
.global _start_mmode
_start_mmode:
    /* Jump over header to M-mode boot code */
    j       _boot_start_mmode
    
    /* Same header structure as _start */
    .word   0                        /* offset 0x04: reserved */
    .dword  0x0                      /* offset 0x08: text_offset */
    .dword  _end - _start_mmode      /* offset 0x10: image_size */
    .dword  0x0                      /* offset 0x18: flags */
    .word   0x2                      /* offset 0x20: version */
    .word   0                        /* offset 0x24: reserved */
    .dword  0                        /* offset 0x28: reserved */
    .ascii  "RISCV\0\0\0"            /* offset 0x30: magic */
    .dword  0                        /* offset 0x38: reserved */

.align 6
_boot_start_mmode:
    /* Save boot parameters (same as S-mode entry) */
    mv s0, a0                   /* s0 = hartid */
    mv s1, a1                   /* s1 = device tree pointer */
    
    /* We're in M-mode - set up S-mode and transition */
    
    /* Configure mstatus for S-mode operation */
    li t0, 0x00001800          /* MPP[12:11] = 01 (S-mode) */
    csrw mstatus, t0
    
    /* Delegate all exceptions and interrupts to S-mode */
    li t0, 0xFFFF              /* Delegate all exceptions */
    csrw medeleg, t0
    li t0, 0xFFFF              /* Delegate all interrupts */
    csrw mideleg, t0
    
    /* Configure PMP to allow S-mode access to all memory */
    /* Set PMP to allow all accesses (R=1, W=1, X=1) for entire address space */
    li t0, 0x1F                /* R=1, W=1, X=1, A=11 (NAPOT), L=0) */
    csrw pmpcfg0, t0
    li t0, -1                  /* Cover entire address space */
    srli t0, t0, 2             /* Adjust for NAPOT encoding */
    csrw pmpaddr0, t0
    
    /* Calculate and store physical base (same as S-mode path) */
    /* TODO: Calculate dynamically - TEMPORARY hardcoded for QEMU */
    li t0, 0x80200000          /* TEMPORARY - hardcoded for QEMU */
    la t1, phys_base_storage
    sd t0, 0(t1)
    
    /* Set up mepc to continue in S-mode at common code */
    /* Jump to the point after privilege mode handling in main flow */
    /* This is right after the physical base storage in the S-mode path */
    la t0, .Lcommon_smode_path
    csrw mepc, t0
    
    /* Return to S-mode */
    mret
