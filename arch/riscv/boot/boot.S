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

_start:

    /* RISC-V "Linux" boot image header: 
     *
     * Some bootloaders rely on this header (U-Boot, etc) for common 
     * commands that are useful or simplify using the kernel binary/image. 
     */

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

/* Actual start needs to be at offset 64 (0x40) from _start */
.align 6
_boot_start:

    /* Save boot parameters/data. 
     * Bootloader passes: a0=hart_idm a1=dtb pointer 
     * Save in registers that are safe 
     */ 
    mv s0, a0                   /* s0 = hartid */
    mv s1, a1                   /* s1 = device tree pointer */
    
    /*
     * Privilege mode handling
     * 
     * Standard entry (_start) assumes S-mode (OpenSBI/UEFI)
     * so assuming we are in S-mode continue with initialization. 
     */
    
.Lget_phys_addr:

    /* Caclculate and store physical base address dynamically. 
     * We use the same approach FreeBSD uses: 
     * compare the physical address of a known location (obtained 
     * via PC-relative addressing) with its virtual address (stored
     * in memory by the linker) to calculate the physical base.
     */

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
     * Set up early stack
     * Load the physical address of our bootstrap stack
     * Stack grows down, so we use the top address
     */
    la sp, boot_stack_top       /* Load stack top address */
    /* Stack is already 16-byte aligned from .align directive */

    /*
     * Clear BSS section
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
     * Set up identity mapping for bootstrap
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
     * - 1 L1 page table for kernel mapping
     * Place them after kernel end with 4KB padding, aligned to page boundaries */
    
    la t0, _kernel_end
    li t1, 0x1000               /* Add 4KB padding for safety */
    add t0, t0, t1
    li t1, PAGE_SIZE - 1
    add t0, t0, t1              /* Round up to page boundary */
    not t1, t1
    and t0, t0, t1              /* t0 = page-aligned address after kernel */
    
    /* Save page table base addresses */
    mv s5, t0                   /* s5 = L2 (root) page table */
    li t1, PAGE_SIZE
    add s6, t0, t1              /* s6 = L1 page table for identity map */
    add s7, s6, t1              /* s7 = L1 page table for kernel mapping */
    
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
    
    /* Clear L1 page table for kernel mapping */
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
    
    /* Always relocate DTB to a consistent location
     * This ensures DTB is always at a known offset from kernel end */
    beqz s1, .Ldtb_ok          /* Skip if no DTB */

    /* Copy DTB to a safe location 64KB after kernel end */
    la t0, _kernel_end          /* Get kernel end address */
    li t1, 0x10000              /* Add 64KB offset for safety */
    add t0, t0, t1
    li t1, PAGE_SIZE - 1
    add t0, t0, t1              /* Round up */
    not t1, t1
    and t0, t0, t1              /* Page-align the destination */
    
    /* Copy DTB header to get size (FDT format, size at offset 4) */
    lwu t1, 4(s1)               /* Load totalsize field (32-bit big-endian, unsigned) */
    
    /* Convert from big-endian to little-endian (32-bit byte swap) */
    
    /* Extract and reposition each byte */
    andi t2, t1, 0xFF           /* t2 = byte 0 (DD) */
    slli t2, t2, 24             /* Move to position 3 */
    
    srli t3, t1, 8
    andi t3, t3, 0xFF           /* t3 = byte 1 (CC) */
    slli t3, t3, 16             /* Move to position 2 */
    
    srli t4, t1, 16
    andi t4, t4, 0xFF           /* t4 = byte 2 (BB) */
    slli t4, t4, 8              /* Move to position 1 */
    
    srli t5, t1, 24             /* t5 = byte 3 (AA), already in position 0 */
    
    /* Combine bytes in new order */
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
     * Set up higher-half kernel mapping
     * Map the kernel from its virtual address (0xFFFFFFFF80200000) to its
     * physical address. We'll use 2MB megapages for efficiency.
     * 
     * Virtual address breakdown for 0xFFFFFFFF80200000 in Sv39:
     * - Bits [38:30] = 0x1FF (L2 index 511, last entry)
     * - Bits [29:21] = 0x001 (L1 index 1)
     * - Bits [20:12] = 0x000 (L0 index 0)
     * - Bits [11:0]  = 0x000 (offset 0)
     */
    
    /* Install L1 page table for kernel mapping in L2 root table
     * The kernel virtual address starts at 0xFFFFFFFF80200000
     * In Sv39, this is sign-extended from 39-bit address 0x1FF80200000
     * L2 index = (0x1FF80200000 >> 30) & 0x1FF = 0x7FE & 0x1FF = 510 */
    li t0, 510                  /* L2 index for kernel space */
    slli t0, t0, 3              /* Multiply by 8 for byte offset */
    add t0, s5, t0              /* t0 = address of L2 PTE */
    
    /* Create L2 PTE pointing to kernel L1 table */
    mv t1, s7                   /* L1 table physical address (for kernel) */
    srli t1, t1, PAGE_SHIFT     /* Convert to PPN */
    slli t1, t1, 10             /* Shift PPN to correct position in PTE */
    ori t1, t1, PTE_V           /* Set valid bit (non-leaf) */
    sd t1, 0(t0)                /* Store L2 PTE */
    
    /* Calculate how much to map
     * We'll map 128MB of kernel space using 2MB megapages, matching the
     * identity mapping size. This ensures everything accessible via identity
     * mapping remains accessible through higher-half addresses */
    li t6, 128 * 1024 * 1024    /* t6 = size to map (128MB) */
    
    /* Fill L1 table with 2MB megapage mappings for kernel
     * Virtual base: 0xFFFFFFFF80200000
     * Physical base: s2 (calculated earlier, e.g., 0x80200000) */
    
    mv t0, s7                   /* L1 table base for kernel mapping */
    mv t1, s2                   /* Physical base address of kernel */
    mv t2, t6                   /* Size to map */
    
    /* Calculate the starting L1 index
     * For VA 0xFFFFFFFF80200000, L1 index = (0x80200000 >> 21) & 0x1FF = 0x401 & 0x1FF = 1
     * Actually, we need to be more careful here. Let's recalculate:
     * The 39-bit portion of VA is: 0x1FF80200000
     * L1 index = (0x80200000 >> 21) & 0x1FF = 1 */
    li t3, 1                    /* Starting L1 index */
    slli t3, t3, 3              /* Convert to byte offset */
    add t0, t0, t3              /* Start at correct L1 entry */
    
.Lkernel_map_loop:
    beqz t2, .Lkernel_map_done
    
    /* Create PTE for 2MB megapage */
    mv t3, t1                   /* Physical address */
    srli t3, t3, PAGE_SHIFT     /* Convert to PPN */
    slli t3, t3, 10             /* Shift PPN to correct position */
    
    /* Set permissions: V, R, W, X, A, D, G for kernel code/data
     * We're using RWX for simplicity now. In production, we'd separate
     * text (RX), rodata (R), and data (RW) sections */
    li t4, PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D | PTE_G
    or t3, t3, t4
    
    sd t3, 0(t0)                /* Store PTE */
    
    /* Move to next 2MB region */
    addi t0, t0, 8              /* Next PTE slot */
    li t5, MEGAPAGE_SIZE
    add t1, t1, t5              /* Next physical address */
    sub t2, t2, t5              /* Decrease remaining size */
    j .Lkernel_map_loop
    
.Lkernel_map_done:

    /*
     * Configure and enable MMU
     * Set up SATP register to enable Sv39 paging with our page tables.
     * After this, all memory accesses will go through the MMU.
     */
    
    /* Prepare SATP value
     * SATP format for Sv39:
     * [63:60] = MODE (8 for Sv39)
     * [59:44] = ASID (0 for kernel)
     * [43:0]  = PPN of root page table (44 bits)
     */
    
    /* Get physical page number of root (L2) table */
    mv t0, s5                   /* s5 = L2 root page table physical address */
    srli t0, t0, PAGE_SHIFT     /* Convert to PPN (shift right by 12) */
    
    /* Set Sv39 mode (MODE = 8) */
    li t1, SATP_MODE_SV39       /* Load mode value (8) */
    slli t1, t1, 60             /* Shift to bits [63:60] */
    
    /* Combine MODE and PPN (ASID is 0 by default) */
    or t0, t0, t1               /* t0 = SATP value */
    
    /* Flush TLB before enabling MMU */
    sfence.vma zero, zero       /* Global TLB flush */
    
    /* Enable paging by writing SATP */
    csrw satp, t0               /* Write SATP and enable MMU */
    
    /* Synchronize - ensure MMU change takes effect */
    sfence.vma zero, zero       /* Flush TLB again after enabling */
    
    /* Assuming we made it past this point, the MMU is enabled. 
     *
     * We're still executing via the identity mapping, so the next 
     * instruction fetch goes through the page tables. 
     *
     * The next step is to jump to the higher-half addresses, where 
     * we will be executing at 0xFFFFFFFF8XXXXXXX addresses instead. 

    /* Calculate the virtual address of higher_half label
     * We need to compute the virtual-physical offset dynamically */
    
    /* Load physical address of higher_half first (PC-relative) */
    lla t0, higher_half         /* t0 = physical address of higher_half */
    
    /* Calculate the virtual-physical offset
     * Virtual kernel base is always 0xFFFFFFFF80200000 (link address)
     * Physical base is in s2 (calculated dynamically at runtime)
     * Offset = virtual_base - physical_base */
    
    /* Load virtual kernel base */
    li t1, -1                   /* Load all 1s */
    slli t1, t1, 32             /* t1 = 0xFFFFFFFF00000000 */
    lui t2, 0x80200             /* Load 0x80200000 into upper bits */
    or t1, t1, t2               /* t1 = 0xFFFFFFFF80200000 (virtual base) */
    
    /* Calculate offset: virtual_base - physical_base */
    sub t1, t1, s2              /* t1 = offset */
    
    /* Add offset to physical address to get virtual address */
    add t0, t0, t1              /* t0 = virtual address of higher_half */
    
    /* Jump to virtual address
     * This is the critical transition to higher-half execution */
    jr t0                       /* Jump to virtual address */
    
    /* In theory, we never reach here, so if we're here, get trapped. */ 
    unimp

higher_half:

    /* Switch stack pointer to virtual address
     * The stack is currently using physical addresses via identity mapping.
     * Convert it to use kernel virtual addresses. */
    
    /* Calculate virtual-physical offset (same as for the jump)
     * Virtual base: 0xFFFFFFFF80200000 (fixed link address)
     * Physical base: s2 (calculated at runtime)
     * Offset = virtual - physical */
    
    /* Load virtual kernel base */
    li t0, -1                   /* Load all 1s */
    slli t0, t0, 32             /* t0 = 0xFFFFFFFF00000000 */
    lui t1, 0x80200             /* Load 0x80200000 into upper bits */
    or t0, t0, t1               /* t0 = 0xFFFFFFFF80200000 (virtual base) */
    
    /* Calculate offset: virtual_base - physical_base */
    sub t0, t0, s2              /* t0 = offset */
    
    /* Add offset to stack pointer */
    add sp, sp, t0              /* sp = sp + offset (now virtual) */
    
    /* Now we're fully in higher-half:
     * - PC is at virtual address
     * - Stack is at virtual address
     */ 
    
    /*
     * Set up trap vector
     * Configure the supervisor trap vector to handle exceptions and interrupts.
     * We'll use direct mode (MODE=0) where all traps jump to the same handler.
     */
    
    /* Load the virtual address of trap_vector */
    la t0, trap_vector          /* Load address of trap handler */
    
    /* Set direct mode (MODE[1:0] = 00) - all exceptions go to BASE address
     * In direct mode, all traps cause pc to be set to BASE */
    andi t0, t0, ~0x3           /* Clear lower 2 bits to ensure MODE=00 */
    
    /* Write to stvec register */
    csrw stvec, t0              /* Set supervisor trap vector */
    
    /* Verify it was set correctly (optional, for safety) */
    csrr t1, stvec
    bne t0, t1, .Ltrap_setup_failed
    
    /* Trap vector successfully configured */
    j .Ltrap_setup_done
    
.Ltrap_setup_failed:
    /* If trap setup failed, hang here for debugging
     * This shouldn't happen, but better safe than sorry */
    wfi
    j .Ltrap_setup_failed
    
.Ltrap_setup_done:

    /*
     * Initialize CPU features
     * 
     * We take a conservative approach similar to FreeBSD/Linux:
     * - FPU: Keep disabled initially (FS=OFF). Will be enabled lazily on first use.
     * - Vector: Keep disabled initially (VS=OFF). Will be enabled lazily if supported.
     * - Interrupts: Keep disabled until scheduler is ready.
     * 
     */
    
    /* Read current sstatus */
    csrr t0, sstatus
    
    /* Clear FS (Floating-point Status) field to disable FPU
     * FS[14:13] = 00 (Off) */
    li t1, 0x6000               /* FS_MASK = (0x3 << 13) */
    not t1, t1                  /* Invert to create clear mask */
    and t0, t0, t1              /* Clear FS bits */
    
    /* Clear VS (Vector Status) field to disable vector unit if it exists
     * VS[10:9] = 00 (Off) - Note: VS only exists in newer specs */
    li t1, 0x600                /* VS_MASK = (0x3 << 9) */
    not t1, t1                  /* Invert to create clear mask */
    and t0, t0, t1              /* Clear VS bits (safe even if VS doesn't exist) */
    
    /* Ensure SUM bit is clear (disable supervisor access to user pages)
     * This is a security feature - supervisor should explicitly enable when needed */
    li t1, (1 << 18)            /* SUM is bit 18 */
    not t1, t1
    and t0, t0, t1              /* Clear SUM */
    
    /* Keep interrupts disabled (SIE=0) until we're ready
     * The scheduler will enable interrupts when it's initialized */
    li t1, (1 << 1)             /* SIE is bit 1 */
    not t1, t1
    and t0, t0, t1              /* Clear SIE */
    
    /* Write back modified sstatus */
    csrw sstatus, t0
    
    /* Clear any pending interrupts */
    csrw sip, zero              /* Clear all pending interrupts */
    csrw sie, zero              /* Disable all interrupt sources for now */
    
    /*
     * Set up boot data for kernel
     * Prepare arguments for init_riscv according to RISC-V calling convention.
     * 
     * RISC-V calling convention:
     * - a0-a7: Function arguments (first 8 integer/pointer arguments)
     * - a0-a1: Also used for return values
     * 
     * init_riscv expects:
     * - a0: hart_id (hardware thread ID)
     * - a1: dtb (device tree blob pointer)
     */
    
    /* Restore boot parameters from callee-saved registers
     * We saved these at the very beginning:
     * - s0 = hart_id
     * - s1 = dtb pointer */
    mv a0, s0                   /* a0 = hart_id */
    mv a1, s1                   /* a1 = device tree pointer */
    
    /* Optional: Store kernel physical base for later use
     * Some kernels need to know their physical load address */
    la t0, kernel_phys_base     /* Load address of storage variable */
    sd s2, 0(t0)                /* Store physical base (s2) */

    /*
     * Jump to kernel main
     * Call init_riscv with the prepared arguments.
     * This function should not return, but we handle it gracefully if it does.
     */
    
    /* Load address of init_riscv function */
    la ra, .Lkernel_returned    /* Set return address for debugging */
    la t0, init_riscv           /* Load address of init_riscv */
    
    /* Jump to init_riscv(hart_id, dtb) */
    jalr ra, t0, 0              /* Call init_riscv, ra = return address */
    
.Lkernel_returned:

    /* We should never get here, but handle it gracefully */

    /*
     * Halt if kernel_main returns
     * If init_riscv unexpectedly returns, safely halt the system.
     */
    
    /* Disable all interrupts to prevent any further processing */
    csrw sie, zero              /* Disable all interrupt sources */
    csrci sstatus, 0x2          /* Clear SIE bit in sstatus */
    
.Lboot_hang:

    /* Infinite halt loop */
    wfi
    j .Lboot_hang

/* DATA SECTION STARTS HERE */

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

/* End of boot.S */
