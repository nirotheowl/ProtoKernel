#include <stdint.h>
#include <stddef.h>
#include <memory/mmu.h>
#include <memory/paging.h>
#include <uart.h>

// Need these macros from paging.h for validation
#define PGDIR_SHIFT             39
#define PTRS_PER_TABLE          512
#define PGDIR_INDEX(va)         (((va) >> PGDIR_SHIFT) & (PTRS_PER_TABLE - 1))


void mmu_init(void) {
    paging_init();
}

// Enable MMU (this will be called from asm 
void enable_mmu(void) {
    uint64_t mair_val;
    uint64_t tcr_val;
    uint64_t sctlr_val;
    
    // Configure MAIR_EL1
    mair_val = ((uint64_t)MAIR_ATTR_DEVICE_nGnRnE << (MT_DEVICE_nGnRnE * 8)) |
               ((uint64_t)MAIR_ATTR_DEVICE_nGnRE << (MT_DEVICE_nGnRE * 8)) |
               ((uint64_t)MAIR_ATTR_NORMAL_NC << (MT_NORMAL_NC * 8)) |
               ((uint64_t)MAIR_ATTR_NORMAL << (MT_NORMAL * 8));
    
    __asm__ volatile("msr mair_el1, %0" : : "r" (mair_val));
    
    // Configure TCR_EL1 with comprehensive settings
    tcr_val = TCR_T0SZ(VA_BITS) |       // 48-bit virtual addresses for TTBR0
              TCR_T1SZ(VA_BITS) |       // 48-bit virtual addresses for TTBR1
              TCR_TG0_4K |              // 4KB granule for TTBR0
              TCR_TG1_4K |              // 4KB granule for TTBR1
              TCR_SH0_INNER |           // Inner shareable for TTBR0
              TCR_SH1_INNER |           // Inner shareable for TTBR1
              TCR_ORGN0_WBWA |          // Outer write-back write-allocate for TTBR0
              TCR_ORGN1_WBWA |          // Outer write-back write-allocate for TTBR1
              TCR_IRGN0_WBWA |          // Inner write-back write-allocate for TTBR0
              TCR_IRGN1_WBWA |          // Inner write-back write-allocate for TTBR1
              TCR_IPS_48BIT |           // 48-bit intermediate physical address
              TCR_EPD1_DISABLE |        // Disable TTBR1 walks (not using upper half yet)
              TCR_AS_16BIT;             // 16-bit ASID size
    
    __asm__ volatile("msr tcr_el1, %0" : : "r" (tcr_val));
    
    // Set TTBR0_EL1 to our page table
    pgd_t* pgd_base = get_kernel_pgd();
    __asm__ volatile("msr ttbr0_el1, %0" : : "r" ((uint64_t)pgd_base));
    
    // Ensure changes are visible
    __asm__ volatile("isb");
    
    // Invalidate TLBs
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb nsh");
    __asm__ volatile("isb");
    
    // Ensure page tables are visible to MMU
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    
    // Enable MMU
    __asm__ volatile("mrs %0, sctlr_el1" : "=r" (sctlr_val));
    
    // Don't enable caches yet - just MMU
    uint64_t new_sctlr = sctlr_val | SCTLR_M;
    
    // Ensure UART write completes before MMU enable
    __asm__ volatile("dsb sy");
    
    // Enable MMU
    __asm__ volatile("msr sctlr_el1, %0" : : "r" (new_sctlr) : "memory");
    __asm__ volatile("isb");
    
    // Add a memory barrier after MMU enable
    __asm__ volatile("dsb sy");
}

// Disable MMU and restore to clean state
void disable_mmu(void) {
    uint64_t sctlr_val;
    
    // Clean and invalidate all caches before disabling MMU
    dcache_clean_invalidate_range(0, ~0UL);
    
    // Ensure cache operations complete
    dsb(sy);
    isb();
    
    // Read current SCTLR_EL1
    __asm__ volatile("mrs %0, sctlr_el1" : "=r" (sctlr_val));
    
    // Clear MMU and cache enable bits
    sctlr_val &= ~(SCTLR_M | SCTLR_C | SCTLR_I);
    
    // Disable MMU
    __asm__ volatile("msr sctlr_el1, %0" : : "r" (sctlr_val));
    isb();
    
    // Invalidate all TLBs
    tlb_invalidate_all();
}

// Cache management functions

// Get cache line size
static inline uint64_t get_dcache_line_size(void) {
    uint64_t ctr_el0;
    __asm__ volatile("mrs %0, ctr_el0" : "=r" (ctr_el0));
    return 4 << ((ctr_el0 >> 16) & 0xF);
}

// Clean data cache by virtual address range
void dcache_clean_range(uint64_t start, uint64_t size) {
    uint64_t line_size = get_dcache_line_size();
    uint64_t addr;
    
    start &= ~(line_size - 1);  // Align to cache line
    
    for (addr = start; addr < start + size; addr += line_size) {
        dc_cvac(addr);
    }
    
    dsb(sy);
}

// Invalidate data cache by virtual address range
void dcache_invalidate_range(uint64_t start, uint64_t size) {
    uint64_t line_size = get_dcache_line_size();
    uint64_t addr;
    
    start &= ~(line_size - 1);  // Align to cache line
    
    for (addr = start; addr < start + size; addr += line_size) {
        dc_ivac(addr);
    }
    
    dsb(sy);
}

// Clean and invalidate data cache by virtual address range
void dcache_clean_invalidate_range(uint64_t start, uint64_t size) {
    uint64_t line_size = get_dcache_line_size();
    uint64_t addr;
    
    start &= ~(line_size - 1);  // Align to cache line
    
    for (addr = start; addr < start + size; addr += line_size) {
        dc_civac(addr);
    }
    
    dsb(sy);
}

// Invalidate all instruction caches
void icache_invalidate_all(void) {
    ic_iallu();
    dsb(sy);
    isb();
}

// TLB management functions

// Invalidate all TLB entries
void tlb_invalidate_all(void) {
    tlbi(vmalle1);
    dsb(nsh);
    isb();
}

// Invalidate TLB entry for a specific virtual address
void tlb_invalidate_page(uint64_t va) {
    va >>= 12;  // Shift to page number
    __asm__ volatile("tlbi vale1, %0" : : "r" (va));
    dsb(nsh);
    isb();
}

// Invalidate TLB entries for a virtual address range
void tlb_invalidate_range(uint64_t va, uint64_t size) {
    uint64_t end = va + size;
    uint64_t page_size = PAGE_SIZE;
    
    // Align to page boundaries
    va &= ~(page_size - 1);
    end = (end + page_size - 1) & ~(page_size - 1);
    
    // Invalidate each page in the range
    for (; va < end; va += page_size) {
        tlb_invalidate_page(va);
    }
}
