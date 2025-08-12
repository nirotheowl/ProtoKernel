/*
 * arch/arm64/mm/vmm_arm64.c
 *
 * ARM64 specific VMM implementation
 */

#include <memory/vmm.h>
#include <memory/vmm_arch.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <arch_mmu.h>
#include <mm/pte.h>
#include <uart.h>
#include <string.h>
#include <stddef.h>

// Architecture constants
const int ARCH_PT_LEVELS = ARM64_PT_LEVELS;        // 4 levels for ARM64
const int ARCH_PT_TOP_LEVEL = ARM64_PT_LEVEL_0;    // Level 0 is top
const int ARCH_PT_LEAF_LEVEL = ARM64_PT_LEVEL_3;   // Level 3 is leaf

// Forward declarations of static functions
static void arm64_vmm_init(void);
static uint64_t* arm64_get_pte(uint64_t *table, uint64_t vaddr, int level);
static uint64_t* arm64_walk_create(struct vmm_context *ctx, uint64_t vaddr, 
                                   int target_level, bool create);
static uint64_t arm64_attrs_to_pte(uint64_t attrs);
static uint64_t arm64_pte_to_attrs(uint64_t pte);
static uint64_t arm64_get_pt_base(void);
static void arm64_set_pt_base(uint64_t phys);
static void arm64_flush_tlb_page(uint64_t vaddr);
static void arm64_flush_tlb_all(void);
static void arm64_barrier(void);
static int arm64_get_pt_levels(void);
static uint64_t arm64_get_pt_index(uint64_t vaddr, int level);
static bool arm64_is_pte_valid(uint64_t pte);
static bool arm64_is_pte_table(uint64_t pte);
static bool arm64_is_pte_block(uint64_t pte, int level);
static uint64_t arm64_make_table_pte(uint64_t phys);
static uint64_t arm64_make_block_pte(uint64_t phys, uint64_t attrs, int level);
static uint64_t arm64_pte_to_phys(uint64_t pte);
static size_t arm64_get_block_size(int level);

// VMM architecture operations for ARM64
const vmm_arch_ops_t vmm_arch_ops = {
    .init = arm64_vmm_init,
    .get_pte = arm64_get_pte,
    .walk_create = arm64_walk_create,
    .attrs_to_pte = arm64_attrs_to_pte,
    .pte_to_attrs = arm64_pte_to_attrs,
    .get_pt_base = arm64_get_pt_base,
    .set_pt_base = arm64_set_pt_base,
    .flush_tlb_page = arm64_flush_tlb_page,
    .flush_tlb_all = arm64_flush_tlb_all,
    .barrier = arm64_barrier,
    .get_pt_levels = arm64_get_pt_levels,
    .get_pt_index = arm64_get_pt_index,
    .is_pte_valid = arm64_is_pte_valid,
    .is_pte_table = arm64_is_pte_table,
    .is_pte_block = arm64_is_pte_block,
    .make_table_pte = arm64_make_table_pte,
    .make_block_pte = arm64_make_block_pte,
    .pte_to_phys = arm64_pte_to_phys,
    .get_block_size = arm64_get_block_size,
};

// Initialize ARM64 specific VMM
static void arm64_vmm_init(void) {
    uint64_t ttbr1 = arch_mmu_get_ttbr1();
    
    uart_puts("ARM64 VMM: TTBR1_EL1 = ");
    uart_puthex(ttbr1);
    uart_puts("\n");
    
    // Extract page table base from TTBR1
    uint64_t pt_base = ttbr1 & ARM64_PTE_ADDR_MASK;
    uart_puts("ARM64 VMM: Page table base = ");
    uart_puthex(pt_base);
    uart_puts("\n");
}

// Get page table entry at specific level
static uint64_t* arm64_get_pte(uint64_t *table, uint64_t vaddr, int level) {
    uint64_t idx;
    
    switch (level) {
    case ARM64_PT_LEVEL_0:  // Top level - bits [47:39]
        idx = (vaddr >> ARM64_VA_L0_SHIFT) & ARM64_VA_INDEX_MASK;
        break;
    case ARM64_PT_LEVEL_1:  // Level 1 - bits [38:30]
        idx = (vaddr >> ARM64_VA_L1_SHIFT) & ARM64_VA_INDEX_MASK;
        break;
    case ARM64_PT_LEVEL_2:  // Level 2 - bits [29:21]
        idx = (vaddr >> ARM64_VA_L2_SHIFT) & ARM64_VA_INDEX_MASK;
        break;
    case ARM64_PT_LEVEL_3:  // Leaf level - bits [20:12]
        idx = (vaddr >> ARM64_VA_L3_SHIFT) & ARM64_VA_INDEX_MASK;
        break;
    default:
        return NULL;
    }
    
    return &table[idx];
}

// Walk page tables, creating entries as needed
static uint64_t* arm64_walk_create(struct vmm_context *ctx, uint64_t vaddr, 
                                   int target_level, bool create) {
    vmm_context_t *context = (vmm_context_t *)ctx;
    uint64_t *table = context->l0_table;
    uint64_t *pte;
    
    // Walk from top level (0) down to target level
    for (int level = ARM64_PT_LEVEL_0; level < target_level; level++) {
        pte = arm64_get_pte(table, vaddr, level);
        
        if (!pte) {
            return NULL;
        }
        
        if (!arm64_is_pte_valid(*pte)) {
            if (!create) {
                return NULL;
            }
            
            // Allocate new table
            uint64_t *new_table = vmm_alloc_page_table();
            if (!new_table) {
                uart_puts("ARM64 VMM: Failed to allocate page table at level ");
                uart_putc('0' + level);
                uart_puts("\n");
                return NULL;
            }
            
            // Set table descriptor
            uint64_t phys = vmm_pt_virt_to_phys(new_table);
            *pte = ARM64_PHYS_TO_PTE(phys) | ARM64_PTE_TYPE_TABLE | ARM64_PTE_VALID;
            
            // Ensure write is visible
            arm64_barrier();
        } else if (arm64_is_pte_block(*pte, level)) {
            // Hit a block entry before reaching target level
            uart_puts("ARM64 VMM: Hit block entry at level ");
            uart_putc('0' + level);
            uart_puts(" while walking to level ");
            uart_putc('0' + target_level);
            uart_puts("\n");
            return NULL;
        }
        
        // Move to next level
        uint64_t next_table_phys = ARM64_PTE_TO_PHYS(*pte);
        table = (uint64_t*)vmm_pt_phys_to_virt(next_table_phys);
    }
    
    return arm64_get_pte(table, vaddr, target_level);
}

// Convert generic VMM attributes to ARM64 PTE flags
static uint64_t arm64_attrs_to_pte(uint64_t attrs) {
    uint64_t pte = ARM64_PTE_AF;  // Always set Access Flag
    
    // Access permissions
    if (attrs & VMM_ATTR_WRITE) {
        pte |= ARM64_PTE_AP_RW;
    } else if (attrs & VMM_ATTR_READ) {
        pte |= ARM64_PTE_AP_RO;
    }
    
    // Execute permissions
    if (!(attrs & VMM_ATTR_EXECUTE)) {
        pte |= ARM64_PTE_UXN | ARM64_PTE_PXN;
    }
    
    // User accessible
    if (attrs & VMM_ATTR_USER) {
        // Adjust access permissions for user access
        if (attrs & VMM_ATTR_WRITE) {
            pte = (pte & ~ARM64_PTE_AP_MASK) | ARM64_PTE_AP_RW_ALL;
        } else {
            pte = (pte & ~ARM64_PTE_AP_MASK) | ARM64_PTE_AP_RO_ALL;
        }
    }
    
    // Memory type
    if (attrs & VMM_ATTR_DEVICE) {
        pte |= ARM64_PTE_ATTR_DEVICE;
    } else if (attrs & VMM_ATTR_NOCACHE) {
        pte |= ARM64_PTE_ATTR_NORMAL_NC;
    } else {
        pte |= ARM64_PTE_ATTR_NORMAL;
    }
    
    // Shareability
    pte |= ARM64_PTE_SH_INNER;
    
    return pte;
}

// Convert ARM64 PTE flags to generic VMM attributes
static uint64_t arm64_pte_to_attrs(uint64_t pte) {
    uint64_t attrs = 0;
    
    // Check access permissions
    uint64_t ap = pte & ARM64_PTE_AP_MASK;
    if (ap == ARM64_PTE_AP_RW || ap == ARM64_PTE_AP_RW_ALL) {
        attrs |= VMM_ATTR_READ | VMM_ATTR_WRITE;
    } else if (ap == ARM64_PTE_AP_RO || ap == ARM64_PTE_AP_RO_ALL) {
        attrs |= VMM_ATTR_READ;
    }
    
    // Check execute permission
    if (!(pte & (ARM64_PTE_UXN | ARM64_PTE_PXN))) {
        attrs |= VMM_ATTR_EXECUTE;
    }
    
    // Check user access
    if (ap == ARM64_PTE_AP_RW_ALL || ap == ARM64_PTE_AP_RO_ALL) {
        attrs |= VMM_ATTR_USER;
    }
    
    // Check memory type
    uint64_t attr_idx = (pte >> 2) & 0x7;
    if (attr_idx == MT_DEVICE_nGnRnE || attr_idx == MT_DEVICE_nGnRE) {
        attrs |= VMM_ATTR_DEVICE;
    } else if (attr_idx == MT_NORMAL_NC) {
        attrs |= VMM_ATTR_NOCACHE;
    }
    
    return attrs;
}

// Get page table base from TTBR1 register
static uint64_t arm64_get_pt_base(void) {
    return arch_mmu_get_ttbr1() & ARM64_PTE_ADDR_MASK;
}

// Set page table base in TTBR1 register
static void arm64_set_pt_base(uint64_t phys) {
    __asm__ volatile("msr TTBR1_EL1, %0" : : "r"(phys));
    arm64_barrier();
}

// Flush TLB for specific virtual address
static void arm64_flush_tlb_page(uint64_t vaddr) {
    arch_mmu_invalidate_page(vaddr);
}

// Flush entire TLB
static void arm64_flush_tlb_all(void) {
    arch_mmu_flush_all();
}

// Memory barrier
static void arm64_barrier(void) {
    arch_mmu_barrier();
}

// Get number of page table levels
static int arm64_get_pt_levels(void) {
    return ARM64_PT_LEVELS;
}

// Get page table index for given level
static uint64_t arm64_get_pt_index(uint64_t vaddr, int level) {
    switch (level) {
    case ARM64_PT_LEVEL_0:
        return (vaddr >> ARM64_VA_L0_SHIFT) & ARM64_VA_INDEX_MASK;
    case ARM64_PT_LEVEL_1:
        return (vaddr >> ARM64_VA_L1_SHIFT) & ARM64_VA_INDEX_MASK;
    case ARM64_PT_LEVEL_2:
        return (vaddr >> ARM64_VA_L2_SHIFT) & ARM64_VA_INDEX_MASK;
    case ARM64_PT_LEVEL_3:
        return (vaddr >> ARM64_VA_L3_SHIFT) & ARM64_VA_INDEX_MASK;
    default:
        return 0;
    }
}

// Check if PTE is valid
static bool arm64_is_pte_valid(uint64_t pte) {
    return ARM64_PTE_IS_VALID(pte);
}

// Check if PTE is a table entry
static bool arm64_is_pte_table(uint64_t pte) {
    return ARM64_PTE_IS_TABLE(pte);
}

// Check if PTE is a block entry
static bool arm64_is_pte_block(uint64_t pte, int level) {
    // Blocks are only valid at levels 1 and 2
    if (level == ARM64_PT_LEVEL_1 || level == ARM64_PT_LEVEL_2) {
        return ARM64_PTE_IS_BLOCK(pte);
    }
    return false;
}

// Create table PTE pointing to next level
static uint64_t arm64_make_table_pte(uint64_t phys) {
    return ARM64_PHYS_TO_PTE(phys) | ARM64_PTE_TYPE_TABLE | ARM64_PTE_VALID;
}

// Create block/page PTE with given attributes
static uint64_t arm64_make_block_pte(uint64_t phys, uint64_t attrs, int level) {
    uint64_t pte = ARM64_PHYS_TO_PTE(phys);
    pte |= arm64_attrs_to_pte(attrs);
    
    // Set appropriate descriptor type
    if (level == ARM64_PT_LEVEL_3) {
        pte |= ARM64_PTE_TYPE_PAGE;  // Page descriptor at L3
    } else {
        pte |= ARM64_PTE_TYPE_BLOCK; // Block descriptor at L1/L2
    }
    
    return pte;
}

// Extract physical address from PTE
static uint64_t arm64_pte_to_phys(uint64_t pte) {
    return ARM64_PTE_TO_PHYS(pte);
}

// Get block size for given level
static size_t arm64_get_block_size(int level) {
    switch (level) {
    case ARM64_PT_LEVEL_1:
        return ARM64_BLOCK_SIZE_L1;  // 1GB
    case ARM64_PT_LEVEL_2:
        return ARM64_BLOCK_SIZE_L2;  // 2MB
    case ARM64_PT_LEVEL_3:
        return ARM64_PAGE_SIZE;      // 4KB
    default:
        return 0;
    }
}