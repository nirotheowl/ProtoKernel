/*
 * arch/riscv/mm/vmm_riscv.c
 *
 * RISC-V specific VMM implementation for Sv39
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

/* Architecture constants */
const int ARCH_PT_LEVELS = RISCV_PT_LEVELS;        /* 3 levels for Sv39 */
const int ARCH_PT_TOP_LEVEL = RISCV_PT_LEVEL_2;    /* Level 2 is top */
const int ARCH_PT_LEAF_LEVEL = RISCV_PT_LEVEL_0;   /* Level 0 is leaf */

/* Forward declarations of static functions */
static void riscv_vmm_init(void);
static uint64_t* riscv_get_pte(uint64_t *table, uint64_t vaddr, int level);
static uint64_t* riscv_walk_create(struct vmm_context *ctx, uint64_t vaddr, 
                                   int target_level, bool create);
static uint64_t riscv_attrs_to_pte(uint64_t attrs);
static uint64_t riscv_pte_to_attrs(uint64_t pte);
static uint64_t riscv_get_pt_base(void);
static void riscv_set_pt_base(uint64_t phys);
static void riscv_flush_tlb_page(uint64_t vaddr);
static void riscv_flush_tlb_all(void);
static void riscv_barrier(void);
static int riscv_get_pt_levels(void);
static uint64_t riscv_get_pt_index(uint64_t vaddr, int level);
static bool riscv_is_pte_valid(uint64_t pte);
static bool riscv_is_pte_table(uint64_t pte);
static bool riscv_is_pte_block(uint64_t pte, int level);
static uint64_t riscv_make_table_pte(uint64_t phys);
static uint64_t riscv_make_block_pte(uint64_t phys, uint64_t attrs, int level);
static uint64_t riscv_pte_to_phys(uint64_t pte);
static size_t riscv_get_block_size(int level);

/* VMM architecture operations for RISC-V */
const vmm_arch_ops_t vmm_arch_ops = {
    .init = riscv_vmm_init,
    .get_pte = riscv_get_pte,
    .walk_create = riscv_walk_create,
    .attrs_to_pte = riscv_attrs_to_pte,
    .pte_to_attrs = riscv_pte_to_attrs,
    .get_pt_base = riscv_get_pt_base,
    .set_pt_base = riscv_set_pt_base,
    .flush_tlb_page = riscv_flush_tlb_page,
    .flush_tlb_all = riscv_flush_tlb_all,
    .barrier = riscv_barrier,
    .get_pt_levels = riscv_get_pt_levels,
    .get_pt_index = riscv_get_pt_index,
    .is_pte_valid = riscv_is_pte_valid,
    .is_pte_table = riscv_is_pte_table,
    .is_pte_block = riscv_is_pte_block,
    .make_table_pte = riscv_make_table_pte,
    .make_block_pte = riscv_make_block_pte,
    .pte_to_phys = riscv_pte_to_phys,
    .get_block_size = riscv_get_block_size,
};

/* Initialize RISC-V specific VMM */
static void riscv_vmm_init(void) {
    uint64_t satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    
    uart_puts("RISC-V VMM: SATP = ");
    uart_puthex(satp);
    uart_puts("\n");
    
    /* Extract page table base from SATP */
    uint64_t pt_base = (satp & SATP_PPN_MASK) << 12;
    uart_puts("RISC-V VMM: Page table base = ");
    uart_puthex(pt_base);
    uart_puts("\n");
}

/* Get page table entry at specific level */
static uint64_t* riscv_get_pte(uint64_t *table, uint64_t vaddr, int level) {
    uint64_t idx;
    
    /* RISC-V Sv39 uses 3 levels with different indexing than ARM64 */
    switch (level) {
    case RISCV_PT_LEVEL_2:  /* Top level - bits [38:30] */
        idx = (vaddr >> RISCV_VA_VPN2_SHIFT) & RISCV_VA_VPN_MASK;
        break;
    case RISCV_PT_LEVEL_1:  /* Middle level - bits [29:21] */
        idx = (vaddr >> RISCV_VA_VPN1_SHIFT) & RISCV_VA_VPN_MASK;
        break;
    case RISCV_PT_LEVEL_0:  /* Leaf level - bits [20:12] */
        idx = (vaddr >> RISCV_VA_VPN0_SHIFT) & RISCV_VA_VPN_MASK;
        break;
    default:
        return NULL;
    }
    
    return &table[idx];
}

/* Walk page tables, creating entries as needed */
static uint64_t* riscv_walk_create(struct vmm_context *ctx, uint64_t vaddr, 
                                   int target_level, bool create) {
    vmm_context_t *context = (vmm_context_t *)ctx;
    uint64_t *table = context->l0_table;  /* In RISC-V, this is actually the top level */
    uint64_t *pte;
    
    /* Walk from top level (2) down to target level */
    for (int level = RISCV_PT_LEVEL_2; level > target_level; level--) {
        pte = riscv_get_pte(table, vaddr, level);
        
        if (!pte) {
            return NULL;
        }
        
        if (!riscv_is_pte_valid(*pte)) {
            if (!create) {
                return NULL;
            }
            
            /* Allocate new table */
            uint64_t *new_table = vmm_alloc_page_table();
            if (!new_table) {
                uart_puts("RISC-V VMM: Failed to allocate page table at level ");
                uart_putc('0' + level);
                uart_puts("\n");
                return NULL;
            }
            
            /* Set table descriptor - in RISC-V, table entries have V bit but no RWX bits */
            uint64_t phys = vmm_pt_virt_to_phys(new_table);
            *pte = RISCV_PHYS_TO_PTE(phys) | RISCV_PTE_V;
            
            /* Ensure write is visible */
            riscv_barrier();
        } else if (riscv_is_pte_block(*pte, level)) {
            /* Hit a leaf entry before reaching target level */
            uart_puts("RISC-V VMM: Hit leaf entry at level ");
            uart_putc('0' + level);
            uart_puts(" while walking to level ");
            uart_putc('0' + target_level);
            uart_puts("\n");
            return NULL;
        }
        
        /* Move to next level */
        uint64_t next_table_phys = RISCV_PTE_TO_PHYS(*pte);
        table = (uint64_t*)vmm_pt_phys_to_virt(next_table_phys);
    }
    
    return riscv_get_pte(table, vaddr, target_level);
}

/* Convert generic VMM attributes to RISC-V PTE flags */
static uint64_t riscv_attrs_to_pte(uint64_t attrs) {
    uint64_t pte = RISCV_PTE_V;  /* Start with Valid bit */
    
    /* Access permissions */
    if (attrs & VMM_ATTR_READ) {
        pte |= RISCV_PTE_R;
    }
    if (attrs & VMM_ATTR_WRITE) {
        pte |= RISCV_PTE_W | RISCV_PTE_D;  /* Set dirty for writable pages */
    }
    if (attrs & VMM_ATTR_EXECUTE) {
        pte |= RISCV_PTE_X;
    }
    
    /* User accessible */
    if (attrs & VMM_ATTR_USER) {
        pte |= RISCV_PTE_U;
    }
    
    /* Set accessed bit by default */
    pte |= RISCV_PTE_A;
    
    /* Device memory doesn't have special PTE bits in RISC-V */
    /* It's handled by Physical Memory Attributes (PMAs) */
    
    return pte;
}

/* Convert RISC-V PTE flags to generic VMM attributes */
static uint64_t riscv_pte_to_attrs(uint64_t pte) {
    uint64_t attrs = 0;
    
    if (pte & RISCV_PTE_R) attrs |= VMM_ATTR_READ;
    if (pte & RISCV_PTE_W) attrs |= VMM_ATTR_WRITE;
    if (pte & RISCV_PTE_X) attrs |= VMM_ATTR_EXECUTE;
    if (pte & RISCV_PTE_U) attrs |= VMM_ATTR_USER;
    
    return attrs;
}

/* Get page table base from SATP register */
static uint64_t riscv_get_pt_base(void) {
    uint64_t satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return (satp & SATP_PPN_MASK) << 12;
}

/* Set page table base in SATP register */
static void riscv_set_pt_base(uint64_t phys) {
    uint64_t satp = SATP_MODE_SV39 | (phys >> 12);
    __asm__ volatile("csrw satp, %0" : : "r"(satp));
    riscv_barrier();
}

/* Flush TLB for specific virtual address */
static void riscv_flush_tlb_page(uint64_t vaddr) {
    arch_mmu_invalidate_page(vaddr);
}

/* Flush entire TLB */
static void riscv_flush_tlb_all(void) {
    arch_mmu_flush_all();
}

/* Memory barrier */
static void riscv_barrier(void) {
    arch_mmu_barrier();
}

/* Get number of page table levels */
static int riscv_get_pt_levels(void) {
    return RISCV_PT_LEVELS;
}

/* Get page table index for given level */
static uint64_t riscv_get_pt_index(uint64_t vaddr, int level) {
    switch (level) {
    case RISCV_PT_LEVEL_2:
        return (vaddr >> RISCV_VA_VPN2_SHIFT) & RISCV_VA_VPN_MASK;
    case RISCV_PT_LEVEL_1:
        return (vaddr >> RISCV_VA_VPN1_SHIFT) & RISCV_VA_VPN_MASK;
    case RISCV_PT_LEVEL_0:
        return (vaddr >> RISCV_VA_VPN0_SHIFT) & RISCV_VA_VPN_MASK;
    default:
        return 0;
    }
}

/* Check if PTE is valid */
static bool riscv_is_pte_valid(uint64_t pte) {
    return RISCV_PTE_IS_VALID(pte);
}

/* Check if PTE is a table entry */
static bool riscv_is_pte_table(uint64_t pte) {
    return RISCV_PTE_IS_TABLE(pte);
}

/* Check if PTE is a block/leaf entry */
static bool riscv_is_pte_block(uint64_t pte, int level) {
    (void)level;  /* All levels can have leaf entries in RISC-V */
    return RISCV_PTE_IS_LEAF(pte);
}

/* Create table PTE pointing to next level */
static uint64_t riscv_make_table_pte(uint64_t phys) {
    return RISCV_PHYS_TO_PTE(phys) | RISCV_PTE_V;
}

/* Create block/page PTE with given attributes */
static uint64_t riscv_make_block_pte(uint64_t phys, uint64_t attrs, int level) {
    (void)level;  /* Used for compatibility with ARM64 which has level-specific descriptors */
    uint64_t pte = RISCV_PHYS_TO_PTE(phys);
    pte |= riscv_attrs_to_pte(attrs);
    return pte;
}

/* Extract physical address from PTE */
static uint64_t riscv_pte_to_phys(uint64_t pte) {
    return RISCV_PTE_TO_PHYS(pte);
}

/* Get block size for given level */
static size_t riscv_get_block_size(int level) {
    switch (level) {
    case RISCV_PT_LEVEL_2:
        return RISCV_GIGAPAGE_SIZE;  /* 1GB */
    case RISCV_PT_LEVEL_1:
        return RISCV_MEGAPAGE_SIZE;  /* 2MB */
    case RISCV_PT_LEVEL_0:
        return RISCV_PAGE_SIZE;      /* 4KB */
    default:
        return 0;
    }
}