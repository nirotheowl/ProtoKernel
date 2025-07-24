.section ".text.boot"

.global _start

/* Import constants from linker script */
.extern KERNEL_VIRT_BASE
.extern KERNEL_PHYS_BASE

/*
 * Higher-Half Kernel Implementation Plan
 * =====================================
 * 
 * This kernel will be loaded at a low physical address but run at a high
 * virtual address (0xFFFF_xxxx_xxxx_xxxx range). We need to:
 * 
 * 1. Boot with MMU off or identity-mapped
 * 2. Create page tables for both identity mapping and higher-half mapping
 * 3. Enable MMU with both mappings active
 * 4. Jump to higher-half addresses
 * 5. Remove identity mapping (done later in C code)
 * 
 * Memory Layout:
 * - Physical: Kernel loaded at 0x40200000 (2MB-aligned)
 * - Virtual:  Kernel will run at KERNBASE (e.g., 0xFFFF_0000_4020_0000)
 * 
 * Page Table Strategy:
 * - TTBR0: Identity mapping for bootstrap (PA = VA)
 *          Later: User space mappings
 * - TTBR1: Higher-half kernel mapping (VA = KERNBASE + PA)
 * 
 * Page Table Hierarchy (4KB pages, 48-bit VA):
 * - L0: 512 entries, each covers 512GB
 * - L1: 512 entries, each covers 1GB  
 * - L2: 512 entries, each covers 2MB
 * - L3: 512 entries, each covers 4KB
 * 
 * For 16KB pages:
 * - L0: Not used with 48-bit VA
 * - L1: 2 entries, each covers 64TB
 * - L2: 8192 entries, each covers 32MB
 * - L3: 8192 entries, each covers 16KB
 */

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

    /*
     * TODO: Step 1 - Get Physical Load Address
     * ========================================
     * Before we can create page tables, we need to know where we are
     * physically loaded. This is needed to:
     * - Create identity mapping (PA = VA)
     * - Calculate offset for higher-half mapping
     * 
     * Method: Use PC-relative addressing
     * Example from FreeBSD:
     *   adr x28, _start     // Get actual physical address
     *   ldr x29, =_start    // Get link-time virtual address
     *   sub x28, x28, x29   // Calculate relocation offset
     */
    
    // Get our actual physical load address
    adr x28, _start         // Get PC-relative address (physical)
    ldr x29, =_start        // Get absolute address from linker (virtual)
    ldr x30, =KERNEL_VIRT_BASE
    sub x29, x29, x30       // Get offset from virtual base
    sub x28, x28, x29       // Calculate physical base address
    // Now x28 = physical address where kernel is loaded

    /*
     * TODO: Step 2 - Create Page Tables
     * =================================
     * We need to create two sets of mappings:
     * 
     * A. Identity mapping (TTBR0):
     *    - Maps physical addresses to themselves
     *    - Needed during MMU enable and transition
     *    - Will map at least the kernel code/data
     *    - Can be coarse (2MB blocks)
     * 
     * B. Higher-half mapping (TTBR1):
     *    - Maps kernel to 0xFFFF_xxxx_xxxx_xxxx range
     *    - This is the permanent kernel mapping
     *    - Should map kernel text as read-only + executable
     *    - Should map kernel data as read-write + non-executable
     * 
     * Page table allocation:
     * - Place page tables before kernel in physical memory
     * - Need tables for: L0, L1, L2, and possibly L3
     * - Clear all page tables first
     * 
     * FreeBSD approach:
     * - Uses 7-8 pages before kernel load address for tables
     * - Creates minimal identity map for bootstrap
     * - Creates full kernel mapping in higher half
     */

    /*
     * TODO: Step 3 - Configure MMU Registers
     * ======================================
     * Set up all the MMU control registers:
     * 
     * MAIR_EL1: Memory Attribute Indirection Register
     * - Define memory types (Device, Normal cached/uncached)
     * 
     * TCR_EL1: Translation Control Register
     * - T0SZ: Size of TTBR0 address space
     * - T1SZ: Size of TTBR1 address space  
     * - TG0/TG1: Translation granule (4KB/16KB/64KB)
     * - Share attributes, cache attributes
     * 
     * TTBR0_EL1: Base address of TTBR0 page tables
     * TTBR1_EL1: Base address of TTBR1 page tables
     */

    /*
     * TODO: Step 4 - Enable MMU
     * ========================
     * Enable the MMU with both mappings active:
     * - Set SCTLR_EL1.M bit
     * - Ensure I-cache and D-cache settings are correct
     * - ISB to synchronize
     * 
     * After this point, we're running with MMU on using
     * identity mapping (low addresses)
     */

    /*
     * TODO: Step 5 - Jump to Higher Half
     * ==================================
     * Transition from identity-mapped low addresses to
     * higher-half virtual addresses:
     * 
     * - Load the higher-half address of next instruction
     * - Use indirect branch to jump there
     * - After jump, we're running in higher half
     * 
     * Example:
     *   ldr x0, =higher_half_label  // Load VA
     *   br x0                       // Jump to VA
     * higher_half_label:
     *   // Now running at 0xFFFF_xxxx_xxxx_xxxx
     */

    /*
     * IMPORTANT: We're currently running at physical addresses with MMU off.
     * The symbols from the linker script are virtual addresses, so we can't
     * use them directly yet. We need to:
     * 1. Set up a physical stack
     * 2. Clear BSS using physical addresses
     * 3. Only use virtual addresses after MMU is enabled
     */

    // Set up stack pointer using physical address
    // We'll place stack just before our code (grows down)
    adr x0, _start              // Get physical address of _start
    sub x0, x0, #0x4000         // 16KB stack below code
    mov sp, x0

    // We'll clear BSS after jumping to higher half when addresses are valid

    // For now, jump to kernel_main using current PC-relative addressing
    // This is temporary - proper implementation will:
    // 1. Set up page tables
    // 2. Enable MMU
    // 3. Jump to higher half
    // 4. Then call kernel_main with virtual addresses
    
    mov x0, x19       // Pass device tree pointer
    // TEMPORARY: Direct branch (will fail once we're truly higher-half)
    bl kernel_main

    // If kernel_main returns, hang
hang:
    wfe
    b hang

/*
 * TODO: Helper Functions for Page Table Creation
 * ==============================================
 * 
 * We'll need several helper functions similar to FreeBSD:
 * 
 * 1. get_phys_load_addr:
 *    - Calculate physical load address using PC-relative addressing
 *    - Store in a register (e.g., x28) for use throughout
 * 
 * 2. create_page_tables:
 *    - Main function to orchestrate page table creation
 *    - Calls helpers to build each level
 *    - Returns TTBR0 and TTBR1 base addresses
 * 
 * 3. build_l0_entry:
 *    - Creates L0 table entries pointing to L1 tables
 * 
 * 4. build_l1_entry:
 *    - Creates L1 table entries pointing to L2 tables
 * 
 * 5. build_l2_block_entry:
 *    - Creates 2MB block mappings at L2 level
 *    - Used for both identity and kernel mappings
 * 
 * 6. build_l3_page_entry (if using 4KB pages):
 *    - Creates 4KB page mappings at L3 level
 *    - Allows finer-grained permissions
 * 
 * 7. enable_mmu:
 *    - Configure MAIR, TCR, TTBR0, TTBR1
 *    - Enable MMU in SCTLR_EL1
 *    - Handle cache/TLB management
 */

/*
 * TODO: Page Table Memory Layout
 * ==============================
 * 
 * Page tables will be placed just before kernel in physical memory:
 * 
 * 0x40200000 - (n * PAGE_SIZE): Page tables start here
 * 0x40200000: Kernel starts here
 * 
 * Tables needed (example for 4KB pages):
 * - 1 page for TTBR1 L0 table
 * - 1 page for TTBR1 L1 table  
 * - 1 page for TTBR1 L2 table
 * - n pages for TTBR1 L3 tables (if needed)
 * - 1 page for TTBR0 L0 table
 * - 1 page for TTBR0 L1 table
 * - 1 page for TTBR0 L2 table
 * 
 * Total: ~7-8 pages minimum
 */

/*
 * TODO: Important Constants and Definitions
 * ========================================
 * 
 * Define these based on your chosen configuration:
 * 
 * KERNBASE: Virtual base address (e.g., 0xFFFF000000000000)
 * PAGE_SIZE: 4KB or 16KB
 * 
 * Table entry formats:
 * - Valid bit (bit 0)
 * - Table/Block/Page descriptor type (bits 0-1)
 * - Memory attributes index (bits 2-4)
 * - Access permissions (bits 6-7)
 * - Shareability (bits 8-9)
 * - Access flag (bit 10)
 * - Not Global (bit 11)
 * - Contiguous (bit 52)
 * - PXN/UXN (bits 53-54)
 * 
 * Memory attributes for MAIR_EL1:
 * - Device memory: nGnRnE, nGnRE
 * - Normal memory: Outer/Inner Write-Back Cacheable
 * - Normal memory: Outer/Inner Non-Cacheable
 */

.section ".data"
.align 12
.global _stack_bottom
_stack_bottom:
    .skip 4096 * 4   // 16KB stack
.global _stack_top
_stack_top:

/*
 * TODO: Page Table Storage Section
 * ================================
 * 
 * Add a section for page tables before kernel:
 * 
 * .section ".init_pagetable", "aw", %nobits
 * .align 12  // 4KB aligned
 * 
 * pagetable_l0_ttbr1:
 *     .skip PAGE_SIZE
 * pagetable_l1_ttbr1:
 *     .skip PAGE_SIZE
 * pagetable_l2_ttbr1:
 *     .skip PAGE_SIZE
 * pagetable_l3_ttbr1:  // If using 4KB pages
 *     .skip PAGE_SIZE * n
 * 
 * pagetable_l0_ttbr0:
 *     .skip PAGE_SIZE
 * pagetable_l1_ttbr0:
 *     .skip PAGE_SIZE  
 * pagetable_l2_ttbr0:
 *     .skip PAGE_SIZE
 * 
 * This section should be placed before .text in the linker script
 */