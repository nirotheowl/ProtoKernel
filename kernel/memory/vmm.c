/*
 * kernel/memory/vmm.c
 *
 * Virtual Memory Manager implementation
 * Handles page table management and virtual memory operations
 */

#include <memory/vmm.h>
#include <memory/pmm.h>
/* paging.h constants now in vmm.h */
#include <memory/vmparam.h>
#include <uart.h>
#include <string.h>
#include <stddef.h>

/* Additional PTE definitions not in paging.h */
#define PTE_ADDR_MASK      0x0000FFFFFFFFF000UL  /* Bits 47:12 for address */
#define PTE_AP_RW_EL1      PTE_AP_RW             /* Alias for clarity */
#define PTE_AP_RO_EL1      PTE_AP_RO             /* Alias for clarity */

/* Memory attribute indices */
#define PTE_ATTR_DEVICE    PTE_ATTRINDX(MT_DEVICE_nGnRnE)
#define PTE_ATTR_NORMAL_NC PTE_ATTRINDX(MT_NORMAL_NC)
#define PTE_ATTR_NORMAL    PTE_ATTRINDX(MT_NORMAL)

/* Global kernel page table context */
static vmm_context_t kernel_context;
static bool vmm_initialized = false;
static bool dmap_ready = false;

/* Page table entry attribute conversion */
static uint64_t vmm_attrs_to_pte(uint64_t attrs) {
    uint64_t pte = PTE_AF;
    
    /* Access permissions */
    if (attrs & VMM_ATTR_WRITE) {
        pte |= PTE_AP_RW_EL1;
    } else if (attrs & VMM_ATTR_READ) {
        pte |= PTE_AP_RO_EL1;
    }
    
    /* Execute permissions */
    if (!(attrs & VMM_ATTR_EXECUTE)) {
        pte |= PTE_UXN | PTE_PXN;
    }
    
    /* Memory type */
    if (attrs & VMM_ATTR_DEVICE) {
        pte |= PTE_ATTR_DEVICE;
    } else if (attrs & VMM_ATTR_NOCACHE) {
        pte |= PTE_ATTR_NORMAL_NC;
    } else {
        pte |= PTE_ATTR_NORMAL;
    }
    
    /* Shareability */
    pte |= PTE_SH_INNER;
    
    return pte;
}

/* Get page table entry at specific level */
static uint64_t* vmm_get_pte(uint64_t *table, uint64_t vaddr, int level) {
    static int get_pte_count = 0;
    get_pte_count++;
    uint64_t idx;
    
    // if (get_pte_count <= 20) {
    //     uart_puts("    vmm_get_pte: vaddr=");
    //     uart_puthex(vaddr);
    //     uart_puts(" level=");
    //     uart_putc('0' + level);
    //     uart_puts(" table=");
    //     uart_puthex((uint64_t)table);
    // }
    
    switch (level) {
    case PT_LEVEL_0:
        idx = (vaddr >> 39) & 0x1FF;
        break;
    case PT_LEVEL_1:
        idx = (vaddr >> 30) & 0x1FF;
        /* Debug: Let's see the actual calculation */
        // if ((vaddr & 0xFFFF000000000000UL) == 0xFFFF000000000000UL) {
        //     uart_puts("VMM: L1 calculation for ");
        //     uart_puthex(vaddr);
        //     uart_puts(" -> shifted=");
        //     uart_puthex(vaddr >> 30);
        //     uart_puts(" -> idx=");
        //     uart_puthex(idx);
        //     uart_puts("\n");
        // }
        break;
    case PT_LEVEL_2:
        idx = (vaddr >> 21) & 0x1FF;
        break;
    case PT_LEVEL_3:
        idx = (vaddr >> 12) & 0x1FF;
        break;
    default:
        return NULL;
    }
    
    // if (get_pte_count <= 20) {
    //     uart_puts(" idx=");
    //     uart_puthex(idx);
    //     uart_puts(" -> &table[");
    //     uart_puthex(idx);
    //     uart_puts("]=");
    //     uart_puthex((uint64_t)&table[idx]);
    //     uart_puts("\n");
    // }
    
    /* Debug output for DMAP */
    // if ((vaddr & 0xFFFFF00000000000UL) == (DMAP_BASE & 0xFFFFF00000000000UL) && level == PT_LEVEL_1) {
    //     uart_puts("VMM: L1 index for DMAP addr ");
    //     uart_puthex(vaddr);
    //     uart_puts(" is ");
    //     uart_puthex(idx);
    //     uart_puts("\n");
    // }
    
    return &table[idx];
}

/* Walk page tables, creating entries as needed */
static uint64_t* vmm_walk_create(vmm_context_t *ctx, uint64_t vaddr, int target_level) {
    /* Debug first few calls */
    static int debug_count = 0;
    // if (debug_count < 10) {
    //     uart_puts("vmm_walk_create: vaddr=");
    //     uart_puthex(vaddr);
    //     uart_puts(" target_level=");
    //     uart_putc('0' + target_level);
    //     uart_puts("\n");
    //     debug_count++;
    // }
    
    uint64_t *table = ctx->l0_table;
    uint64_t *pte;
    
    /* Both TTBR0 and TTBR1 start from L0 */
    int start_level = PT_LEVEL_0;
    
    for (int level = start_level; level < target_level; level++) {
        // if (debug_count <= 10) {
        //     uart_puts("  Level ");
        //     uart_putc('0' + level);
        //     uart_puts(": table=");
        //     uart_puthex((uint64_t)table);
        //     uart_puts("\n");
        // }
        
        pte = vmm_get_pte(table, vaddr, level);
        
        /* Debug: Check if pte is valid before dereferencing */
        if (!pte) {
            uart_puts("VMM: vmm_get_pte returned NULL at level ");
            uart_putc('0' + level);
            uart_puts("\n");
            return NULL;
        }
        
        // if (debug_count <= 10) {
        //     uart_puts("  PTE at ");
        //     uart_puthex((uint64_t)pte);
        //     uart_puts(" = ");
        //     uart_puthex(*pte);
        //     uart_puts("\n");
        // }
        
        if (!(*pte & PTE_VALID)) {
            /* Allocate new table */
            uint64_t *new_table = vmm_alloc_page_table();
            if (!new_table) {
                uart_puts("VMM: Failed to allocate page table at level ");
                uart_putc('0' + level);
                uart_puts(" for vaddr ");
                uart_puthex(vaddr);
                uart_puts("\n");
                return NULL;
            }
            
            /* Set table descriptor */
            uint64_t phys = VIRT_TO_PHYS((uint64_t)new_table);
            *pte = phys | PTE_TYPE_TABLE | PTE_VALID;
            
            /* Ensure write is visible */
            __asm__ volatile("dsb ishst" ::: "memory");
        }
        
        /* Move to next level */
        uint64_t next_table_phys = *pte & PTE_ADDR_MASK;
        table = (uint64_t*)PHYS_TO_VIRT(next_table_phys);
    }
    
    return vmm_get_pte(table, vaddr, target_level);
}

/* Initialize VMM subsystem */
void vmm_init(void) {
    // uart_puts("\nInitializing Virtual Memory Manager...\n");
    
    /* Get current TTBR1_EL1 value (kernel page table) */
    uint64_t ttbr1;
    __asm__ volatile("mrs %0, TTBR1_EL1" : "=r"(ttbr1));
    
    /* Extract page table base (bits 47:12) */
    uint64_t pt_phys = ttbr1 & 0x0000FFFFFFFFF000UL;
    
    /* Set up kernel context
     * TTBR1 points to the L0 table, just like TTBR0
     */
    kernel_context.l0_table = (uint64_t*)PHYS_TO_VIRT(pt_phys);
    kernel_context.phys_base = pt_phys;
    kernel_context.is_kernel = true;
    
    // uart_puts("VMM: Kernel page table (L0) at ");
    // uart_puthex((uint64_t)kernel_context.l0_table);
    // uart_puts(" (phys ");
    // uart_puthex(pt_phys);
    // uart_puts(")\n");
    
    vmm_initialized = true;
}

/* Allocate a new page table from PMM */
uint64_t* vmm_alloc_page_table(void) {
    static int alloc_count = 0;
    alloc_count++;
    
    // uart_puts("vmm_alloc_page_table #");
    // uart_puthex(alloc_count);
    // uart_puts(": calling pmm_alloc_page\n");
    
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        // uart_puts("  pmm_alloc_page returned 0!\n");
        return NULL;
    }
    
    // uart_puts("  Got physical page at ");
    // uart_puthex(phys);
    // uart_puts("\n");
    
    /* Convert to virtual address and clear */
    uint64_t *table = (uint64_t*)PHYS_TO_VIRT(phys);
    // uart_puts("  Virtual address: ");
    // uart_puthex((uint64_t)table);
    // uart_puts("\n");
    
    // uart_puts("  Clearing page with memset...\n");
    memset(table, 0, PAGE_SIZE);
    
    // uart_puts("  Page table allocated successfully\n");
    return table;
}

/* Free a page table back to PMM */
void vmm_free_page_table(uint64_t *table) {
    uint64_t phys = VIRT_TO_PHYS((uint64_t)table);
    pmm_free_page(phys);
}

/* Map a single page with given attributes */
bool vmm_map_page(vmm_context_t *ctx, uint64_t vaddr, uint64_t paddr, uint64_t attrs) {
    if (!ctx || (vaddr & (PAGE_SIZE - 1)) || (paddr & (PAGE_SIZE - 1))) {
        uart_puts("VMM: map_page invalid params - ctx=");
        uart_puthex((uint64_t)ctx);
        uart_puts(" vaddr=");
        uart_puthex(vaddr);
        uart_puts(" paddr=");
        uart_puthex(paddr);
        uart_puts("\n");
        return false;
    }
    
    /* Walk/create page tables to L3 */
    uint64_t *pte = vmm_walk_create(ctx, vaddr, PT_LEVEL_3);
    if (!pte) {
        return false;
    }
    
    /* Check if already mapped */
    if (*pte & PTE_VALID) {
        uart_puts("VMM: Warning - page already mapped at ");
        uart_puthex(vaddr);
        uart_puts("\n");
        return false;
    }
    
    /* Create PTE */
    *pte = paddr | vmm_attrs_to_pte(attrs) | PTE_TYPE_PAGE | PTE_VALID;
    
    /* Ensure write is visible before TLB invalidate */
    __asm__ volatile("dsb ishst" ::: "memory");
    
    /* Invalidate TLB for this address */
    vmm_flush_tlb_page(vaddr);
    
    return true;
}

/* Map a range of pages (optimizes for large pages when possible) */
bool vmm_map_range(vmm_context_t *ctx, uint64_t vaddr, uint64_t paddr, 
                   size_t size, uint64_t attrs) {
    if (!ctx || (vaddr & (PAGE_SIZE - 1)) || (paddr & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
        uart_puts("VMM: Invalid parameters to map_range\n");
        uart_puts("  vaddr=");
        uart_puthex(vaddr);
        uart_puts(" paddr=");
        uart_puthex(paddr);
        uart_puts(" size=");
        uart_puthex(size);
        uart_puts("\n");
        return false;
    }
    
    uint64_t end_vaddr = vaddr + size;
    uint64_t pte_attrs = vmm_attrs_to_pte(attrs);
    uint64_t mapped = 0;
    uint64_t pages_mapped = 0;
    
    /* Cache for L3 table to avoid repeated walks */
    uint64_t *cached_l3_table = NULL;
    uint64_t cached_l3_base = 0;
    
    while (vaddr < end_vaddr) {
        // if ((pages_mapped % 8192) == 0 && pages_mapped > 0) {
        //     uart_puts("  Mapped ");
        //     uart_puthex(pages_mapped);
        //     uart_puts(" pages (");
        //     uart_puthex(pages_mapped * PAGE_SIZE);
        //     uart_puts(" bytes) so far...\n");
        // }
        /* Try 2MB block mapping if aligned and size permits */
        // if (pages_mapped == 0) {
        //     uart_puts("  Block check: vaddr=");
        //     uart_puthex(vaddr);
        //     uart_puts(" paddr=");
        //     uart_puthex(paddr);
        //     uart_puts(" vaddr&~MASK=");
        //     uart_puthex(vaddr & ~SECTION_MASK);
        //     uart_puts(" paddr&~MASK=");
        //     uart_puthex(paddr & ~SECTION_MASK);
        //     uart_puts(" size=");
        //     uart_puthex(end_vaddr - vaddr);
        //     uart_puts("\n");
        // }
        if (!(vaddr & ~SECTION_MASK) && !(paddr & ~SECTION_MASK) && 
            (end_vaddr - vaddr) >= SECTION_SIZE) {
            
            // if (pages_mapped == 0) {
            //     uart_puts("VMM: Using 2MB blocks for mapping\n");
            // }
            
            /* Walk/create to L2 */
            uint64_t *pte = vmm_walk_create(ctx, vaddr, PT_LEVEL_2);
            if (!pte) {
                uart_puts("VMM: Failed to walk/create to L2 for ");
                uart_puthex(vaddr);
                uart_puts("\n");
                return false;
            }
            
            /* Check if already mapped */
            if (*pte & PTE_VALID) {
                uart_puts("VMM: Warning - 2MB block already mapped at ");
                uart_puthex(vaddr);
                uart_puts("\n");
                return false;
            }
            
            /* Create block descriptor */
            *pte = paddr | pte_attrs | PTE_TYPE_BLOCK | PTE_VALID;
            
            vaddr += SECTION_SIZE;
            paddr += SECTION_SIZE;
            mapped += SECTION_SIZE;
            pages_mapped += (SECTION_SIZE / PAGE_SIZE);
        } else {
            /* Fall back to 4KB pages - optimize with L3 cache */
            if (pages_mapped == 0) {
                // uart_puts("VMM: Using 4KB pages for mapping\n");
            }
            uint64_t l3_base = vaddr & ~((1UL << 21) - 1);  /* L3 table covers 2MB */
            
            /* Check if we need a new L3 table */
            if (!cached_l3_table || l3_base != cached_l3_base) {
                /* Walk to L2 to get L3 table */
                if (pages_mapped == 0) {
                    // uart_puts("VMM: Walking to L2 for vaddr ");
                    // uart_puthex(vaddr);
                    // uart_puts("\n");
                }
                uint64_t *pte = vmm_walk_create(ctx, vaddr, PT_LEVEL_2);
                if (!pte) {
                    uart_puts("VMM: Failed to walk to L2 for ");
                    uart_puthex(vaddr);
                    uart_puts("\n");
                    return false;
                }
                
                /* Get or create L3 table */
                if (!(*pte & PTE_VALID)) {
                    static int l3_count = 0;
                    l3_count++;
                    // uart_puts("VMM: Allocating L3 table #");
                    // uart_puthex(l3_count);
                    // uart_puts(" for vaddr ");
                    // uart_puthex(vaddr);
                    // uart_puts("\n");
                    
                    uint64_t *new_table = vmm_alloc_page_table();
                    if (!new_table) {
                        uart_puts("VMM: Failed to allocate L3 table\n");
                        return false;
                    }
                    uint64_t phys = VIRT_TO_PHYS((uint64_t)new_table);
                    // uart_puts("  New L3 table at VA ");
                    // uart_puthex((uint64_t)new_table);
                    // uart_puts(" PA ");
                    // uart_puthex(phys);
                    // uart_puts("\n");
                    *pte = phys | PTE_TYPE_TABLE | PTE_VALID;
                    __asm__ volatile("dsb ishst" ::: "memory");
                }
                
                // uart_puts("  About to cache L3 table, *pte=");
                // uart_puthex(*pte);
                // uart_puts("\n");
                
                /* Cache the L3 table */
                uint64_t l3_phys = *pte & PTE_ADDR_MASK;
                // uart_puts("  L3 phys addr: ");
                // uart_puthex(l3_phys);
                // uart_puts("\n");
                
                cached_l3_table = (uint64_t*)PHYS_TO_VIRT(l3_phys);
                cached_l3_base = l3_base;
                
                // uart_puts("  Cached L3 table at VA ");
                // uart_puthex((uint64_t)cached_l3_table);
                // uart_puts("\n");
            }
            
            /* Now map directly in cached L3 table */
            // if (pages_mapped < 5) {
            //     uart_puts("  Mapping page at vaddr ");
            //     uart_puthex(vaddr);
            //     uart_puts("\n");
            // }
            
            int l3_idx = (vaddr >> 12) & 0x1FF;
            // if (pages_mapped < 5) {
            //     uart_puts("  L3 index: ");
            //     uart_puthex(l3_idx);
            //     uart_puts(" PTE addr: ");
            //     uart_puthex((uint64_t)&cached_l3_table[l3_idx]);
            //     uart_puts("\n");
            // }
            
            uint64_t *pte = &cached_l3_table[l3_idx];
            
            if (*pte & PTE_VALID) {
                uart_puts("VMM: Page already mapped at ");
                uart_puthex(vaddr);
                uart_puts("\n");
                return false;
            }
            
            *pte = paddr | pte_attrs | PTE_TYPE_PAGE | PTE_VALID;
            
            vaddr += PAGE_SIZE;
            paddr += PAGE_SIZE;
            mapped += PAGE_SIZE;
            pages_mapped++;
        }
    }
    
    /* Ensure all writes are visible */
    __asm__ volatile("dsb ishst" ::: "memory");
    
    // uart_puts("VMM: Successfully mapped ");
    // uart_puthex(pages_mapped);
    // uart_puts(" pages\n");
    
    return true;
}

/* Unmap a single page */
bool vmm_unmap_page(vmm_context_t *ctx, uint64_t vaddr) {
    if (!ctx || (vaddr & PAGE_MASK)) {
        return false;
    }
    
    /* Walk page tables without creating */
    uint64_t *table = ctx->l0_table;
    uint64_t *pte;
    int start_level = PT_LEVEL_0;
    
    for (int level = start_level; level < PT_LEVEL_3; level++) {
        pte = vmm_get_pte(table, vaddr, level);
        
        if (!(*pte & PTE_VALID)) {
            return false;  /* Not mapped */
        }
        
        if (*pte & PTE_TYPE_BLOCK) {
            /* Found block mapping at this level */
            *pte = 0;
            vmm_flush_tlb_page(vaddr);
            return true;
        }
        
        table = (uint64_t*)PHYS_TO_VIRT(*pte & PTE_ADDR_MASK);
    }
    
    /* Check L3 entry */
    pte = vmm_get_pte(table, vaddr, PT_LEVEL_3);
    if (!(*pte & PTE_VALID)) {
        return false;
    }
    
    /* Clear mapping */
    *pte = 0;
    
    /* Ensure write is visible before TLB invalidate */
    __asm__ volatile("dsb ishst" ::: "memory");
    
    /* Invalidate TLB */
    vmm_flush_tlb_page(vaddr);
    
    return true;
}

/* Unmap a range of pages */
bool vmm_unmap_range(vmm_context_t *ctx, uint64_t vaddr, size_t size) {
    if (!ctx || (vaddr & PAGE_MASK) || (size & PAGE_MASK)) {
        return false;
    }
    
    uint64_t end_vaddr = vaddr + size;
    
    while (vaddr < end_vaddr) {
        if (!vmm_unmap_page(ctx, vaddr)) {
            return false;
        }
        vaddr += PAGE_SIZE;
    }
    
    return true;
}

/* Create the DMAP region for all physical memory */
void vmm_create_dmap(void) {
    // uart_puts("\nCreating DMAP region...\n");
    
    if (!vmm_initialized) {
        uart_puts("VMM: Error - VMM not initialized\n");
        return;
    }
    
    /* Get total physical memory from PMM */
    uint64_t phys_start = PHYS_BASE;
    uint64_t phys_end = pmm_get_memory_end();
    uint64_t size = phys_end - phys_start;
    
    // uart_puts("VMM: Mapping physical memory ");
    // uart_puthex(phys_start);
    // uart_puts(" - ");
    // uart_puthex(phys_end);
    // uart_puts(" to DMAP\n");
    
    /* Map all physical memory to DMAP region */
    uint64_t dmap_start = PHYS_TO_DMAP(phys_start);
    
    // uart_puts("VMM: DMAP will start at ");
    // uart_puthex(dmap_start);
    // uart_puts("\n");
    
    if (!vmm_map_range(&kernel_context, dmap_start, phys_start, size, 
                       VMM_ATTR_RW)) {
        uart_puts("VMM: Failed to create DMAP!\n");
        return;
    }
    
    // uart_puts("VMM: DMAP created at ");
    // uart_puthex(dmap_start);
    // uart_puts(" - ");
    // uart_puthex(dmap_start + size);
    // uart_puts("\n");
    
    /* Flush entire TLB to ensure mappings are active */
    vmm_flush_tlb_all();
    
    /* Test DMAP accessibility before marking as ready */
    // uart_puts("VMM: Testing DMAP accessibility...\n");
    volatile uint64_t *test_addr = (volatile uint64_t *)dmap_start;
    // uart_puts("  Reading from DMAP start: ");
    // uart_puthex((uint64_t)test_addr);
    // uart_puts("\n");
    uint64_t test_val = *test_addr;
    // uart_puts("  Read value: ");
    // uart_puthex(test_val);
    // uart_puts("\n");
    // uart_puts("  Writing test pattern...\n");
    *test_addr = 0xDEADBEEF12345678ULL;
    // uart_puts("  Reading back...\n");
    test_val = *test_addr;
    if (test_val != 0xDEADBEEF12345678ULL) {
        uart_puts("VMM: DMAP test failed! Read ");
        uart_puthex(test_val);
        uart_puts("\n");
        return;
    }
    // uart_puts("  DMAP test passed!\n");
    
    /* Mark DMAP as ready */
    dmap_ready = true;
}

/* Get current kernel page table context */
vmm_context_t* vmm_get_kernel_context(void) {
    return &kernel_context;
}

/* Walk page tables and return physical address for a virtual address */
uint64_t vmm_virt_to_phys(vmm_context_t *ctx, uint64_t vaddr) {
    if (!ctx) {
        return 0;
    }
    
    uint64_t *table = ctx->l0_table;
    uint64_t *pte;
    int start_level = PT_LEVEL_0;
    
    for (int level = start_level; level <= PT_LEVEL_3; level++) {
        pte = vmm_get_pte(table, vaddr, level);
        
        if (!(*pte & PTE_VALID)) {
            return 0;  /* Not mapped */
        }
        
        if (level == PT_LEVEL_3 || (*pte & PTE_TYPE_BLOCK)) {
            /* Found mapping */
            uint64_t mask = (level == PT_LEVEL_3) ? PAGE_MASK : 
                           (level == PT_LEVEL_2) ? SECTION_MASK : 0x3FFFFFFF;
            return (*pte & PTE_ADDR_MASK) | (vaddr & ~mask);
        }
        
        table = (uint64_t*)PHYS_TO_VIRT(*pte & PTE_ADDR_MASK);
    }
    
    return 0;
}

/* Check if a virtual address is mapped */
bool vmm_is_mapped(vmm_context_t *ctx, uint64_t vaddr) {
    return vmm_virt_to_phys(ctx, vaddr) != 0;
}

/* Flush TLB for a specific address */
void vmm_flush_tlb_page(uint64_t vaddr) {
    __asm__ volatile(
        "tlbi vae1is, %0\n"
        "dsb ish\n"
        "isb\n"
        : : "r" (vaddr >> 12) : "memory"
    );
}

/* Flush entire TLB */
void vmm_flush_tlb_all(void) {
    __asm__ volatile(
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        ::: "memory"
    );
}

/* Debug: print page table entries for an address */
void vmm_debug_walk(vmm_context_t *ctx, uint64_t vaddr) {
    if (!ctx) {
        return;
    }
    
    uart_puts("\nPage table walk for ");
    uart_puthex(vaddr);
    uart_puts(":\n");
    
    uint64_t *table = ctx->l0_table;
    uint64_t *pte;
    int start_level = PT_LEVEL_0;
    
    for (int level = start_level; level <= PT_LEVEL_3; level++) {
        pte = vmm_get_pte(table, vaddr, level);
        
        uart_puts("  L");
        uart_putc('0' + level);
        uart_puts(" entry: ");
        uart_puthex(*pte);
        
        if (!(*pte & PTE_VALID)) {
            uart_puts(" (invalid)\n");
            break;
        }
        
        if (level == PT_LEVEL_3 || (*pte & PTE_TYPE_BLOCK)) {
            uart_puts(" (mapped)\n");
            break;
        }
        
        uart_puts(" (table)\n");
        table = (uint64_t*)PHYS_TO_VIRT(*pte & PTE_ADDR_MASK);
    }
}

/* Check if DMAP is ready for use */
bool vmm_is_dmap_ready(void) {
    return dmap_ready;
}