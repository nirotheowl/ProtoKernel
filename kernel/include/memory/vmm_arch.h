/*
 * kernel/include/memory/vmm_arch.h
 *
 * Architecture-specific VMM interface
 * Each architecture must implement these functions
 */

#ifndef _VMM_ARCH_H_
#define _VMM_ARCH_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declarations */
struct vmm_context;

/*
 * Architecture-specific VMM operations
 * Each architecture must provide an implementation
 */
typedef struct vmm_arch_ops {
    /* Initialize architecture-specific VMM */
    void (*init)(void);
    
    /* Get page table entry at specific level */
    uint64_t* (*get_pte)(uint64_t *table, uint64_t vaddr, int level);
    
    /* Walk page tables, creating entries as needed */
    uint64_t* (*walk_create)(struct vmm_context *ctx, uint64_t vaddr, 
                            int target_level, bool create);
    
    /* Convert generic VMM attributes to architecture-specific PTE flags */
    uint64_t (*attrs_to_pte)(uint64_t attrs);
    
    /* Convert architecture-specific PTE flags to generic VMM attributes */
    uint64_t (*pte_to_attrs)(uint64_t pte);
    
    /* Get page table base address from CPU registers */
    uint64_t (*get_pt_base)(void);
    
    /* Set page table base address in CPU registers */
    void (*set_pt_base)(uint64_t phys);
    
    /* Flush TLB for specific virtual address */
    void (*flush_tlb_page)(uint64_t vaddr);
    
    /* Flush entire TLB */
    void (*flush_tlb_all)(void);
    
    /* Memory barrier for page table updates */
    void (*barrier)(void);
    
    /* Ensure page table entry is visible to MMU (arch-specific cache ops) */
    void (*ensure_pte_visible)(void *pte);
    
    /* Get number of page table levels for this architecture */
    int (*get_pt_levels)(void);
    
    /* Get page table index for given level */
    uint64_t (*get_pt_index)(uint64_t vaddr, int level);
    
    /* Check if PTE is valid */
    bool (*is_pte_valid)(uint64_t pte);
    
    /* Check if PTE is a table/branch entry */
    bool (*is_pte_table)(uint64_t pte);
    
    /* Check if PTE is a block/leaf entry */
    bool (*is_pte_block)(uint64_t pte, int level);
    
    /* Create table PTE pointing to next level */
    uint64_t (*make_table_pte)(uint64_t phys);
    
    /* Create block/page PTE with given attributes */
    uint64_t (*make_block_pte)(uint64_t phys, uint64_t attrs, int level);
    
    /* Extract physical address from PTE */
    uint64_t (*pte_to_phys)(uint64_t pte);
    
    /* Get block size for given level */
    size_t (*get_block_size)(int level);
} vmm_arch_ops_t;

/* Each architecture provides this */
extern const vmm_arch_ops_t vmm_arch_ops;

/* Architecture-specific page table level definitions */
/* Architectures define their own levels, e.g.:
 * ARM64: 4 levels (0-3)
 * RISC-V Sv39: 3 levels (0-2)
 * RISC-V Sv48: 4 levels (0-3)
 */

/* Architecture must define these constants */
extern const int ARCH_PT_LEVELS;        /* Total number of page table levels */
extern const int ARCH_PT_TOP_LEVEL;     /* Top level index (usually 0) */
extern const int ARCH_PT_LEAF_LEVEL;    /* Leaf level index (ARCH_PT_LEVELS - 1) */

#endif /* _VMM_ARCH_H_ */