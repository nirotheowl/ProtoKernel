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
     * Calculate and store physical base address dynamically
     * We use the FreeBSD approach: compare the physical address of a known
     * location (obtained via PC-relative addressing) with its virtual address
     * (stored in memory by the linker) to calculate the physical base.
     */
.Lget_phys_addr:
    /* Get physical address of virt_map label using PC-relative addressing
     * Since MMU is off, this gives us the actual physical address */
    lla t0, virt_map           /* t0 = physical address of virt_map */
    
    /* Load the virtual address that the linker assigned to virt_map */
    ld t1, 0(t0)               /* t1 = virtual address of virt_map */
    
    /* Calculate physical-to-virtual offset
     * offset = virtual - physical */
    sub t2, t1, t0             /* t2 = virt - phys offset */
    
    /* Calculate physical base of kernel
     * _start is at the kernel base address (virtual) = 0xFFFFFFFF80200000
     * Physical base = _start (virtual) - offset */
    li t3, -1                  /* Load all 1s */
    slli t3, t3, 32            /* Shift to get 0xFFFFFFFF00000000 */
    lui t4, 0x80200            /* Load 0x80200000 into upper bits */
    or t3, t3, t4              /* t3 = 0xFFFFFFFF80200000 */
    sub t0, t3, t2             /* t0 = physical base address */
    
    /* Store physical base for later use
     * We need to calculate physical address of storage location too */
    lla t1, phys_base_storage  /* t1 = physical addr of phys_base_storage */
    sd t0, 0(t1)               /* Store physical base */

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
     * 
     * We'll create an identity mapping that covers the kernel and DTB.
     * This allows us to continue executing after enabling the MMU.
     * We'll use 2MB megapages for efficiency where possible.
     */
    
    /* Get physical base address we stored earlier */
    la t0, phys_base_storage
    ld s2, 0(t0)                /* s2 = kernel physical base (e.g., 0x80200000) */
    
    /* Calculate the region to identity map
     * We align down to 2MB boundary to use megapages efficiently */
    mv s3, s2                   /* Copy kernel base */
    li t1, MEGAPAGE_SIZE - 1    /* 2MB - 1 = 0x1FFFFF */
    not t1, t1                  /* Create mask 0xFFFFFFFFFFE00000 */
    and s3, s3, t1              /* s3 = start of 2MB-aligned region */
    
    /* We'll map 128MB starting from the aligned base
     * This should cover kernel, initial stack, and typical DTB locations */
    li s4, 128 * 1024 * 1024    /* s4 = size to map (128MB) */
    
    /* Allocate and clear page table pages
     * We need:
     * - 1 L2 (root) page table
     * - 1 L1 page table for identity mapping
     * Place them after kernel end, aligned to page boundaries */
    
    la t0, _kernel_end
    li t1, PAGE_SIZE - 1
    add t0, t0, t1              /* Round up to page boundary */
    not t1, t1
    and t0, t0, t1              /* t0 = page-aligned address after kernel */
    
    /* Save page table base addresses */
    mv s5, t0                   /* s5 = L2 (root) page table */
    li t1, PAGE_SIZE
    add s6, t0, t1              /* s6 = L1 page table for identity map */
    add s7, s6, t1              /* s7 = L1 page table for kernel mapping (Step 6) */
    
    /* Clear L2 (root) page table */
    mv t0, s5
    li t1, PAGE_SIZE
    add t1, t0, t1              /* End address */
.Lclear_l2:
    sd zero, 0(t0)
    addi t0, t0, 8
    blt t0, t1, .Lclear_l2
    
    /* Clear L1 page table for identity mapping */
    mv t0, s6
    li t1, PAGE_SIZE
    add t1, t0, t1
.Lclear_l1_identity:
    sd zero, 0(t0)
    addi t0, t0, 8
    blt t0, t1, .Lclear_l1_identity
    
    /* Clear L1 page table for kernel mapping (will use in Step 6) */
    mv t0, s7
    li t1, PAGE_SIZE
    add t1, t0, t1
.Lclear_l1_kernel:
    sd zero, 0(t0)
    addi t0, t0, 8
    blt t0, t1, .Lclear_l1_kernel
    
    /* Install L1 page table in L2 for identity mapping
     * For address 0x80000000, L2 index = (0x80000000 >> 30) & 0x1FF = 2
     * We calculate the actual index based on our aligned base */
    
    mv t0, s3                   /* Get aligned physical base */
    srli t0, t0, GIGAPAGE_SHIFT /* Shift right by 30 to get L2 index */
    andi t0, t0, 0x1FF          /* Mask to get 9-bit index */
    slli t0, t0, 3              /* Multiply by 8 for byte offset */
    add t0, s5, t0              /* t0 = address of L2 PTE */
    
    /* Create L2 PTE pointing to L1 table */
    mv t1, s6                   /* L1 table physical address */
    srli t1, t1, PAGE_SHIFT     /* Convert to PPN */
    slli t1, t1, 10             /* Shift PPN to correct position in PTE */
    ori t1, t1, PTE_V           /* Set valid bit */
    sd t1, 0(t0)                /* Store L2 PTE */
    
    /* Fill L1 table with 2MB megapage mappings for identity map
     * Each L1 entry maps 2MB */
    
    mv t0, s6                   /* L1 table base */
    mv t1, s3                   /* Physical address to map */
    mv t2, s4                   /* Size to map */
    
.Lidentity_map_loop:
    beqz t2, .Lidentity_map_done
    
    /* Create PTE for 2MB megapage */
    mv t3, t1                   /* Physical address */
    srli t3, t3, PAGE_SHIFT     /* Convert to PPN */
    slli t3, t3, 10             /* Shift PPN to correct position */
    
    /* Set permissions: V, R, W, X, A, D, G for kernel code/data */
    li t4, PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_G
    or t3, t3, t4
    
    /* Calculate L1 index from current physical address */
    mv t4, t1                   /* Current physical address */
    srli t4, t4, MEGAPAGE_SHIFT /* Shift by 21 to get L1 index */
    andi t4, t4, 0x1FF          /* Mask to 9 bits */
    slli t4, t4, 3              /* Multiply by 8 for byte offset */
    add t4, s6, t4              /* Address in L1 table */
    
    sd t3, 0(t4)                /* Store PTE */
    
    /* Move to next 2MB region */
    li t5, MEGAPAGE_SIZE
    add t1, t1, t5              /* Next physical address */
    sub t2, t2, t5              /* Decrease remaining size */
    j .Lidentity_map_loop
    
.Lidentity_map_done:
    
    /* Check if DTB needs relocation
     * The DTB pointer is in s1 */
    beqz s1, .Ldtb_ok          /* Skip if no DTB */
    
    /* Check if DTB is within our mapped region (s3 to s3+s4) */
    blt s1, s3, .Ldtb_needs_copy      /* DTB below mapped region */
    add t0, s3, s4
    bge s1, t0, .Ldtb_needs_copy      /* DTB above mapped region */
    j .Ldtb_ok                         /* DTB is in mapped region */
    
.Ldtb_needs_copy:
    /* Copy DTB to a safe location after page tables */
    li t0, PAGE_SIZE
    li t1, 3                    /* We allocated 3 page tables */
    mul t0, t0, t1
    add t0, s5, t0              /* t0 = address after page tables */
    li t1, 0x1000               /* Add 4KB safety margin */
    add t0, t0, t1
    li t1, PAGE_SIZE - 1
    add t0, t0, t1              /* Round up */
    not t1, t1
    and t0, t0, t1              /* Page-align the destination */
    
    /* Copy DTB header to get size (FDT format, size at offset 4) */
    lw t1, 4(s1)                /* Load totalsize field (32-bit big-endian) */
    
    /* Convert from big-endian to little-endian */
    slli t2, t1, 56
    srli t2, t2, 56             /* Byte 0 -> position 0 */
    slli t3, t1, 40
    srli t3, t3, 48             /* Byte 1 -> position 1 */
    slli t4, t1, 24
    srli t4, t4, 40             /* Byte 2 -> position 2 */
    srli t5, t1, 24             /* Byte 3 -> position 3 */
    slli t5, t5, 24
    slli t3, t3, 8
    slli t4, t4, 16
    or t1, t2, t3
    or t1, t1, t4
    or t1, t1, t5               /* t1 = size in little-endian */
    
    /* Copy DTB data */
    mv t2, s1                   /* Source */
    mv t3, t0                   /* Destination */
    mv t4, t1                   /* Size */
    
.Lcopy_dtb_loop:
    beqz t4, .Lcopy_dtb_done
    lb t5, 0(t2)
    sb t5, 0(t3)
    addi t2, t2, 1
    addi t3, t3, 1
    addi t4, t4, -1
    j .Lcopy_dtb_loop
    
.Lcopy_dtb_done:
    mv s1, t0                   /* Update DTB pointer to new location */
    
.Ldtb_ok:

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

/* Storage for dynamically detected physical base address */
.align 3
.global phys_base_storage
phys_base_storage:
    .dword 0

/* Virtual address marker for physical base calculation
 * This stores the link-time virtual address of this location */
.align 3
virt_map:
    .dword virt_map

/* Bootstrap stack for early initialization */
.align 4  /* Align to 16 bytes for RISC-V ABI */
boot_stack_bottom:
    .space 0x4000  /* 16KB stack */
.global boot_stack_top
boot_stack_top:

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
    
    /* Set up mepc to continue in S-mode at common code */
    /* This will jump to .Lcommon_smode_path which calculates physical base */
    la t0, .Lcommon_smode_path
    csrw mepc, t0
    
    /* Return to S-mode */
    mret
