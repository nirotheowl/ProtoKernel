#include <stdint.h>
#include <stddef.h>
#include <memory/paging.h>
#include <memory/pmm.h>
#include <memory/memmap.h>
#include <uart.h>

// Initial page table for boot - need at least one set to get started
// After PMM is initialized, allocate new ones dynamically
__attribute__((aligned(4096))) static pgd_t boot_pgd[PTRS_PER_TABLE];

// Keep track of allocated page tables for statistics
static uint64_t page_tables_allocated = 0;


static void clear_page_table(void* table) {
    uint64_t* ptr = (uint64_t*)table;
    for (int i = 0; i < PTRS_PER_TABLE; i++) {
        ptr[i] = 0;
    }
}

// Allocate a new page table
static void* alloc_page_table(void) {
    uint64_t pa = pmm_alloc_page_table();
    if (!pa) {
        return NULL;
    }
    
    page_tables_allocated++;
    
    // Page is already cleared by PMM
    return (void*)pa;
}

// Get or allocate a page table entry
static uint64_t* get_or_alloc_table_entry(uint64_t* parent_entry, const char* level_name) {
    (void)level_name;  // Unused in production
    
    if (*parent_entry & PTE_VALID) {
        // Table already exists
        return (uint64_t*)(*parent_entry & PAGE_MASK);
    }
    
    // Need to allocate a new table
    void* new_table = alloc_page_table();
    if (!new_table) {
        return NULL;
    }
    
    // Install the new table
    *parent_entry = ((uint64_t)new_table) | PTE_TYPE_TABLE;
    
    return (uint64_t*)new_table;
}

// Map a single 4KB page with dynamic allocation
void map_page(uint64_t va, uint64_t pa, uint64_t attrs) {
    uint32_t pgd_idx = PGDIR_INDEX(va);
    uint32_t pud_idx = PUD_INDEX(va);
    uint32_t pmd_idx = PMD_INDEX(va);
    uint32_t pte_idx = PTE_INDEX(va);
    
    // Get or allocate PUD
    pud_t* pud = get_or_alloc_table_entry(&boot_pgd[pgd_idx], "PUD");
    if (!pud) return;
    
    // Get or allocate PMD
    pmd_t* pmd = get_or_alloc_table_entry(&pud[pud_idx], "PMD");
    if (!pmd) return;
    
    // Check if PMD already has a block mapping
    if ((pmd[pmd_idx] & PTE_VALID) && !(pmd[pmd_idx] & PTE_TABLE)) {
        return;  // Cannot create 4KB page in 2MB block region
    }
    
    // Get or allocate PTE table
    pte_t* pte = get_or_alloc_table_entry(&pmd[pmd_idx], "PTE");
    if (!pte) return;
    
    // Set PTE entry
    pte[pte_idx] = (pa & PAGE_MASK) | attrs | PTE_AF | PTE_VALID;
}

// Map memory using 4KB pages
void map_pages(uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs) {
    uint64_t end_va = va + size;
    
    while (va < end_va) {
        map_page(va, pa, attrs);
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }
}

// Map a range using 2MB blocks where possible
void map_range(uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs) {
    uint64_t end_va = va + size;
    
    while (va < end_va) {
        uint32_t pgd_idx = PGDIR_INDEX(va);
        uint32_t pud_idx = PUD_INDEX(va);
        uint32_t pmd_idx = PMD_INDEX(va);
        
        // Get or allocate PUD
        pud_t* pud = get_or_alloc_table_entry(&boot_pgd[pgd_idx], "PUD");
        if (!pud) return;
        
        // Get or allocate PMD
        pmd_t* pmd = get_or_alloc_table_entry(&pud[pud_idx], "PMD");
        if (!pmd) return;
        
        // Create 2MB block mapping at PMD level
        uint64_t block_addr = pa & 0xFFFFFFFFFFE00000ULL;
        pmd[pmd_idx] = block_addr | attrs | PTE_TYPE_BLOCK | PTE_AF;
        
        va += (1UL << PMD_SHIFT);  // 2MB
        pa += (1UL << PMD_SHIFT);
    }
}

// Initialize page tables with dynamic allocation
void paging_init(void) {
    // Clear boot PGD
    clear_page_table(boot_pgd);
    
    // Identity map kernel code and data 
    map_range(0x40000000, 0x40000000, 0x400000, PTE_KERNEL_BLOCK);
    
    // Map additional memory for allocator (up to 256MB)
    map_range(0x40400000, 0x40400000, 0xFC00000, PTE_KERNEL_BLOCK);  // Map up to 0x50000000
    
    // Map UART as device memory
    map_range(0x09000000, 0x09000000, 0x200000, PTE_DEVICE_BLOCK);
}

// Map memory intelligently using the best page size
void map_memory(uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs) {
    uint64_t end_va = va + size;
    
    // Handle unaligned start with 4KB pages
    while (va < end_va && (va & (SECTION_SIZE - 1))) {
        if ((end_va - va) < PAGE_SIZE) break;
        map_page(va, pa, attrs);
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }
    
    // Map 2MB blocks where possible
    while ((end_va - va) >= SECTION_SIZE && 
           !(va & (SECTION_SIZE - 1)) && 
           !(pa & (SECTION_SIZE - 1))) {
        
        uint32_t pgd_idx = PGDIR_INDEX(va);
        uint32_t pud_idx = PUD_INDEX(va);
        uint32_t pmd_idx = PMD_INDEX(va);
        
        // Get or allocate PUD
        pud_t* pud = get_or_alloc_table_entry(&boot_pgd[pgd_idx], "PUD");
        if (!pud) return;
        
        // Get or allocate PMD
        pmd_t* pmd = get_or_alloc_table_entry(&pud[pud_idx], "PMD");
        if (!pmd) return;
        
        // Create 2MB block mapping
        pmd[pmd_idx] = (pa & 0xFFFFFFFFFFE00000ULL) | attrs | PTE_TYPE_BLOCK | PTE_AF;
        
        va += SECTION_SIZE;
        pa += SECTION_SIZE;
    }
    
    // Handle remaining with 4KB pages
    while (va < end_va) {
        map_page(va, pa, attrs);
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }
}

// Get the kernel page directory base address
pgd_t* get_kernel_pgd(void) {
    return boot_pgd;
}

