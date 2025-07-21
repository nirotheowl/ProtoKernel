#include <stdint.h>
#include <stddef.h>
#include <memory/paging.h>
#include <uart.h>

// Statically allocated page tables (must be 4KB aligned)
// Allocate enough for initial kernel mappings
__attribute__((aligned(4096))) static pgd_t kernel_pgd[PTRS_PER_TABLE];
__attribute__((aligned(4096))) static pud_t kernel_pud[PTRS_PER_TABLE];
__attribute__((aligned(4096))) static pmd_t kernel_pmd[PTRS_PER_TABLE];
__attribute__((aligned(4096))) static pte_t kernel_pte[PTRS_PER_TABLE];

// Additional page tables for device mappings
__attribute__((aligned(4096))) static pmd_t device_pmd[PTRS_PER_TABLE];

static void print_hex_val(const char* label, uint64_t val) {
    uart_puts(label);
    uart_puts(": 0x");
    uart_puthex(val);
    uart_puts("\n");
}

static void clear_page_table(void* table) {
    uint64_t* ptr = (uint64_t*)table;
    for (int i = 0; i < PTRS_PER_TABLE; i++) {
        ptr[i] = 0;
    }
}


// Map a single 4KB page
void map_page(uint64_t va, uint64_t pa, uint64_t attrs) {
    uart_puts("Mapping VA 0x");
    uart_puthex(va);
    uart_puts(" to PA 0x");
    uart_puthex(pa);
    uart_puts("\n");
    
    uint32_t pgd_idx = PGDIR_INDEX(va);
    uint32_t pud_idx = PUD_INDEX(va);
    uint32_t pmd_idx = PMD_INDEX(va);
    uint32_t pte_idx = PTE_INDEX(va);
    
    // For now, use pre-allocated static tables
    // In a real implementation, allocate these dynamically
    // TODO: dynamic page table allocation 

    // Ensure PGD entry exists
    if (!(kernel_pgd[pgd_idx] & PTE_VALID)) {
        kernel_pgd[pgd_idx] = ((uint64_t)kernel_pud) | PTE_TYPE_TABLE;
    }
    
    // Ensure PUD entry exists
    if (!(kernel_pud[pud_idx] & PTE_VALID)) {
        kernel_pud[pud_idx] = ((uint64_t)kernel_pmd) | PTE_TYPE_TABLE;
    }
    
    // Ensure PMD entry exists
    if (!(kernel_pmd[pmd_idx] & PTE_VALID)) {
        kernel_pmd[pmd_idx] = ((uint64_t)kernel_pte) | PTE_TYPE_TABLE;
    }
    
    // Set PTE entry
    kernel_pte[pte_idx] = pa | attrs | PTE_AF;
}

// Map a range using 2MB blocks where possible
void map_range(uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs) {
    uart_puts("\nMapping range:\n");
    print_hex_val("  VA start", va);
    print_hex_val("  PA start", pa); 
    print_hex_val("  Size", size);
    print_hex_val("  Attributes", attrs);
    
    // Decode attributes for debugging
    uart_puts("  Attribute breakdown:\n");
    uart_puts("    - Valid: ");
    uart_puts((attrs & PTE_VALID) ? "yes" : "no");
    uart_puts("\n    - Type: ");
    if (attrs & PTE_TYPE_BLOCK) {
        uart_puts("block");
    } else if (attrs & PTE_TYPE_TABLE) {
        uart_puts("table");
    } else {
        uart_puts("page");
    }
    uart_puts("\n    - MAIR index: ");
    uart_puthex((attrs >> 2) & 0x7);
    uart_puts("\n    - Shareability: ");
    uint64_t sh = (attrs >> 8) & 0x3;
    if (sh == 0) uart_puts("non-shareable");
    else if (sh == 2) uart_puts("outer shareable");
    else if (sh == 3) uart_puts("inner shareable");
    else uart_puts("reserved");
    uart_puts("\n");
    
    uint64_t end_va = va + size;
    
    // For simplicity, use 2MB blocks for now
    while (va < end_va) {
        uint32_t pgd_idx = PGDIR_INDEX(va);
        uint32_t pud_idx = PUD_INDEX(va);
        uint32_t pmd_idx = PMD_INDEX(va);
        
        // Ensure PGD entry exists
        if (!(kernel_pgd[pgd_idx] & PTE_VALID)) {
            kernel_pgd[pgd_idx] = ((uint64_t)kernel_pud) | PTE_TYPE_TABLE;
            uart_puts("  Created PGD entry\n");
        }
        
        // Ensure PUD entry exists
        if (!(kernel_pud[pud_idx] & PTE_VALID)) {
            // For device mappings, use device_pmd
            // Check if this is device memory by looking at the attribute index bits
            uint64_t attr_idx = (attrs >> 2) & 0x7;
            if (attr_idx == MT_DEVICE_nGnRnE || attr_idx == MT_DEVICE_nGnRE) {
                kernel_pud[pud_idx] = ((uint64_t)device_pmd) | PTE_TYPE_TABLE;
            } else {
                kernel_pud[pud_idx] = ((uint64_t)kernel_pmd) | PTE_TYPE_TABLE;
            }
            uart_puts("  Created PUD entry\n");
        }
        
        // Create 2MB block mapping at PMD level
        // Check if this is device memory by looking at the attribute index bits
        uint64_t attr_idx = (attrs >> 2) & 0x7;
        pmd_t* pmd = (attr_idx == MT_DEVICE_nGnRnE || attr_idx == MT_DEVICE_nGnRE) ? device_pmd : kernel_pmd;
        // For 2MB blocks, bits [47:21] contain the physical address
        uint64_t block_addr = pa & 0xFFFFFFFFFFE00000ULL;
        pmd[pmd_idx] = block_addr | attrs | PTE_TYPE_BLOCK | PTE_AF;
        
        uart_puts("  Created 2MB block at PMD[");
        uart_puthex(pmd_idx);
        uart_puts("] = 0x");
        uart_puthex(pmd[pmd_idx]);
        uart_puts(" (PA=0x");
        uart_puthex(block_addr);
        uart_puts(")\n");
        
        va += (1UL << PMD_SHIFT);  // 2MB
        pa += (1UL << PMD_SHIFT);
    }
}

// Initialize page tables
void paging_init(void) {
    uart_puts("\n=== Page Table Initialization ===\n");
    
    // Clear all page tables
    uart_puts("Clearing page tables...\n");
    clear_page_table(kernel_pgd);
    clear_page_table(kernel_pud);
    clear_page_table(kernel_pmd);
    clear_page_table(kernel_pte);
    clear_page_table(device_pmd);
    
    // Print page table addresses
    print_hex_val("PGD address", (uint64_t)kernel_pgd);
    print_hex_val("PUD address", (uint64_t)kernel_pud);
    print_hex_val("PMD address", (uint64_t)kernel_pmd);
    print_hex_val("PTE address", (uint64_t)kernel_pte);
    
    // Identity map kernel code and data 
    // We need to map from 0x40000000 to cover the page tables in BSS at 0x40088000
    uart_puts("\nIdentity mapping kernel and page tables...\n");
    map_range(0x40000000, 0x40000000, 0x400000, PTE_KERNEL_BLOCK);
    
    // Map UART as device memory (2MB block to cover the region)
    uart_puts("\nMapping UART device memory...\n");
    map_range(0x09000000, 0x09000000, 0x200000, PTE_DEVICE_BLOCK);
    
    uart_puts("\nPage table setup complete!\n");
}

// Get the kernel page directory base address
pgd_t* get_kernel_pgd(void) {
    return kernel_pgd;
}
