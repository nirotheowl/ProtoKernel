/*
 * kernel/memory/vmm.c
 *
 * Architecture-independent Virtual Memory Manager implementation
 * Uses architecture-specific operations defined in vmm_arch.h
 */

#include <memory/vmm.h>
#include <memory/vmm_arch.h>
#include <memory/pmm.h>
#include <memory/vmparam.h>
#include <drivers/fdt.h>
#include <uart.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* Global DMAP physical memory bounds */
uint64_t dmap_phys_base = 0;
uint64_t dmap_phys_max = 0;

/* Global kernel page table context */
static vmm_context_t kernel_context;
static bool vmm_initialized = false;
static bool dmap_ready = false;

/* Helper to convert page table physical address to virtual 
 * Boot page tables are in kernel physical range and use PHYS_TO_VIRT
 * New page tables allocated from PMM need DMAP access
 */
void* vmm_pt_phys_to_virt(uint64_t phys) {
    /* Check if in kernel physical range (boot page tables only) */
    if (phys >= kernel_phys_base && phys < (kernel_phys_base + 0x40000)) {
        /* Boot page tables - use kernel virtual mapping */
        return (void*)PHYS_TO_VIRT(phys);
    } else if (dmap_ready) {
        /* New page tables - use DMAP */
        uint64_t va = PHYS_TO_DMAP(phys);
        if (va == 0) {
            uart_puts("ERROR: pt_phys_to_virt - PHYS_TO_DMAP failed for PA ");
            uart_puthex(phys);
            uart_puts("\n");
            return NULL;
        }
        return (void*)va;
    } else {
        /* Before DMAP, use kernel virtual mapping if in extended kernel range */
        if (phys >= kernel_phys_base && phys < (kernel_phys_base + 0x8000000)) {
            return (void*)PHYS_TO_VIRT(phys);
        }
        /* Otherwise assume identity mapped */
        return (void*)phys;
    }
}

/* Helper to convert page table virtual address to physical */
uint64_t vmm_pt_virt_to_phys(void *virt) {
    uint64_t va = (uint64_t)virt;
    
    /* Check if in DMAP range first */
    if (va >= DMAP_BASE && va < (DMAP_BASE + (dmap_phys_max - dmap_phys_base))) {
        /* In DMAP - convert to physical */
        return va - DMAP_BASE + dmap_phys_base;
    } else if (va >= KERNEL_VIRT_BASE && va < (KERNEL_VIRT_BASE + 0x40000000)) {
        /* In kernel virtual range */
        return VIRT_TO_PHYS(va);
    } else {
        /* Assume identity mapped */
        return va;
    }
}

/* Initialize VMM subsystem */
void vmm_init(void) {
    uart_puts("VMM: Initializing...\n");
    
    /* Call architecture-specific initialization */
    vmm_arch_ops.init();
    
    /* Get current page table base */
    uint64_t pt_phys = vmm_arch_ops.get_pt_base();
    
    uart_puts("VMM: Got page table base: ");
    uart_puthex(pt_phys);
    uart_puts("\n");
    
    /* Set up kernel context - boot page tables use kernel virtual mapping */
    kernel_context.l0_table = (uint64_t*)PHYS_TO_VIRT(pt_phys);
    kernel_context.phys_base = pt_phys;
    kernel_context.is_kernel = true;
    
    uart_puts("VMM: Kernel page table at ");
    uart_puthex((uint64_t)kernel_context.l0_table);
    uart_puts(" (phys ");
    uart_puthex(pt_phys);
    uart_puts(")\n");
    
    vmm_initialized = true;
    uart_puts("VMM: Initialization complete\n");
}

/* Allocate a new page table from PMM */
uint64_t* vmm_alloc_page_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        return NULL;
    }
    
    /* Convert to virtual address and clear */
    uint64_t *table = (uint64_t*)vmm_pt_phys_to_virt(phys);
    memset(table, 0, PAGE_SIZE);
    
    return table;
}

/* Free a page table back to PMM */
void vmm_free_page_table(uint64_t *table) {
    uint64_t phys = vmm_pt_virt_to_phys(table);
    pmm_free_page(phys);
}

/* Architecture-independent walk/create helper */
static uint64_t* vmm_walk_create_internal(vmm_context_t *ctx, uint64_t vaddr, int target_level) {
    return vmm_arch_ops.walk_create((struct vmm_context *)ctx, vaddr, target_level, true);
}

/* Map a single page with given attributes */
bool vmm_map_page(vmm_context_t *ctx, uint64_t vaddr, uint64_t paddr, uint64_t attrs) {
    if (!ctx || (vaddr & (PAGE_SIZE - 1)) || (paddr & (PAGE_SIZE - 1))) {
        uart_puts("VMM: map_page invalid params\n");
        return false;
    }
    
    /* Walk/create page tables to leaf level */
    uint64_t *pte = vmm_walk_create_internal(ctx, vaddr, ARCH_PT_LEAF_LEVEL);
    if (!pte) {
        return false;
    }
    
    /* Check if already mapped */
    if (vmm_arch_ops.is_pte_valid(*pte)) {
        uart_puts("VMM: Warning - page already mapped at ");
        uart_puthex(vaddr);
        uart_puts("\n");
        return false;
    }
    
    /* Create PTE using architecture-specific function */
    *pte = vmm_arch_ops.make_block_pte(paddr, attrs, ARCH_PT_LEAF_LEVEL);
    
    /* Ensure write is visible before TLB invalidate */
    vmm_arch_ops.barrier();
    
    /* Invalidate TLB for this address */
    vmm_flush_tlb_page(vaddr);
    
    return true;
}

/* Map a range of pages (optimizes for large pages when possible) */
bool vmm_map_range(vmm_context_t *ctx, uint64_t vaddr, uint64_t paddr, 
                   size_t size, uint64_t attrs) {
    if (!ctx || (vaddr & (PAGE_SIZE - 1)) || (paddr & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
        uart_puts("VMM: Invalid parameters to map_range\n");
        return false;
    }
    
    uint64_t end_vaddr = vaddr + size;
    uint64_t pages_mapped = 0;
    
    while (vaddr < end_vaddr) {
        /* Try to use largest possible block size for efficiency */
        int best_level = ARCH_PT_LEAF_LEVEL;
        size_t remaining = end_vaddr - vaddr;
        
        /* Check each level from largest to smallest 
         * Note: On RISC-V, levels go 2->1->0 (large to small)
         *       On ARM64, levels go 0->1->2->3 (small to large)
         * We need to iterate in the correct direction for each arch */
        if (ARCH_PT_TOP_LEVEL > ARCH_PT_LEAF_LEVEL) {
            /* RISC-V style: top > leaf, iterate downward */
            for (int level = ARCH_PT_TOP_LEVEL; level >= ARCH_PT_LEAF_LEVEL; level--) {
                size_t block_size = vmm_arch_ops.get_block_size(level);
                if (block_size == 0) continue;
                
                /* Check if addresses are aligned and enough space remains */
                if (!(vaddr & (block_size - 1)) && 
                    !(paddr & (block_size - 1)) && 
                    remaining >= block_size) {
                    best_level = level;
                    break;
                }
            }
        } else {
            /* ARM64 style: top < leaf, iterate upward */
            for (int level = ARCH_PT_TOP_LEVEL; level <= ARCH_PT_LEAF_LEVEL; level++) {
                size_t block_size = vmm_arch_ops.get_block_size(level);
                if (block_size == 0) continue;
                
                /* Check if addresses are aligned and enough space remains */
                if (!(vaddr & (block_size - 1)) && 
                    !(paddr & (block_size - 1)) && 
                    remaining >= block_size) {
                    best_level = level;
                    break;
                }
            }
        }
        
        /* Map at the best level */
        size_t block_size = vmm_arch_ops.get_block_size(best_level);
        
        if (best_level == ARCH_PT_LEAF_LEVEL) {
            /* Regular page mapping */
            if (!vmm_map_page(ctx, vaddr, paddr, attrs)) {
                uart_puts("VMM: Failed to map page at ");
                uart_puthex(vaddr);
                uart_puts("\n");
                return false;
            }
        } else {
            /* Block mapping */
            uint64_t *pte = vmm_walk_create_internal(ctx, vaddr, best_level);
            if (!pte) {
                uart_puts("VMM: Failed to get PTE for block at ");
                uart_puthex(vaddr);
                uart_puts("\n");
                return false;
            }
            
            /* Check if already mapped */
            if (vmm_arch_ops.is_pte_valid(*pte)) {
                uart_puts("VMM: Warning - block already mapped at ");
                uart_puthex(vaddr);
                uart_puts("\n");
                return false;
            }
            
            /* Create block PTE */
            *pte = vmm_arch_ops.make_block_pte(paddr, attrs, best_level);
        }
        
        vaddr += block_size;
        paddr += block_size;
        pages_mapped += block_size / PAGE_SIZE;
    }
    
    /* Ensure all writes are visible before TLB flush */
    vmm_arch_ops.barrier();
    
    /* Flush TLB for entire range */
    vmm_flush_tlb_all();
    
    return true;
}

/* Unmap a single page */
bool vmm_unmap_page(vmm_context_t *ctx, uint64_t vaddr) {
    if (!ctx || (vaddr & (PAGE_SIZE - 1))) {
        return false;
    }
    
    /* Walk page tables without creating */
    uint64_t *pte = vmm_arch_ops.walk_create((struct vmm_context *)ctx, vaddr, ARCH_PT_LEAF_LEVEL, false);
    if (!pte || !vmm_arch_ops.is_pte_valid(*pte)) {
        return false;
    }
    
    /* Clear PTE */
    *pte = 0;
    
    /* Ensure write is visible */
    vmm_arch_ops.barrier();
    
    /* Invalidate TLB */
    vmm_flush_tlb_page(vaddr);
    
    return true;
}

/* Unmap a range of pages */
bool vmm_unmap_range(vmm_context_t *ctx, uint64_t vaddr, size_t size) {
    if (!ctx || (vaddr & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
        return false;
    }
    
    uint64_t end_vaddr = vaddr + size;
    
    while (vaddr < end_vaddr) {
        vmm_unmap_page(ctx, vaddr);
        vaddr += PAGE_SIZE;
    }
    
    return true;
}

/* Walk page tables and return physical address for a virtual address */
uint64_t vmm_virt_to_phys(vmm_context_t *ctx, uint64_t vaddr) {
    if (!ctx) {
        return 0;
    }
    
    /* Walk to leaf level */
    uint64_t *pte = vmm_arch_ops.walk_create((struct vmm_context *)ctx, vaddr, ARCH_PT_LEAF_LEVEL, false);
    if (!pte || !vmm_arch_ops.is_pte_valid(*pte)) {
        return 0;
    }
    
    /* Extract physical address and add page offset */
    uint64_t phys = vmm_arch_ops.pte_to_phys(*pte);
    return phys | (vaddr & (PAGE_SIZE - 1));
}

/* Check if a virtual address is mapped */
bool vmm_is_mapped(vmm_context_t *ctx, uint64_t vaddr) {
    if (!ctx) {
        return false;
    }
    
    uint64_t *pte = vmm_arch_ops.walk_create((struct vmm_context *)ctx, vaddr, ARCH_PT_LEAF_LEVEL, false);
    return pte && vmm_arch_ops.is_pte_valid(*pte);
}

/* Flush TLB for a specific address */
void vmm_flush_tlb_page(uint64_t vaddr) {
    vmm_arch_ops.flush_tlb_page(vaddr);
}

/* Flush entire TLB */
void vmm_flush_tlb_all(void) {
    vmm_arch_ops.flush_tlb_all();
}

/* Get current kernel page table context */
vmm_context_t* vmm_get_kernel_context(void) {
    return &kernel_context;
}

/* Check if DMAP is ready for use */
bool vmm_is_dmap_ready(void) {
    return dmap_ready;
}

/* Create the DMAP region for all physical memory */
void vmm_create_dmap(memory_info_t *mem_info) {
    if (!vmm_initialized) {
        uart_puts("VMM: Cannot create DMAP - VMM not initialized\n");
        return;
    }
    
    uart_puts("VMM: Creating DMAP region...\n");
    
    if (mem_info->count == 0) {
        uart_puts("VMM: No memory regions found\n");
        return;
    }
    
    /* Find the lowest and highest physical addresses across all regions */
    uint64_t lowest_base = UINT64_MAX;
    uint64_t highest_end = 0;
    
    for (int i = 0; i < mem_info->count; i++) {
        uint64_t region_base = mem_info->regions[i].base;
        uint64_t region_end = region_base + mem_info->regions[i].size;
        
        if (region_base < lowest_base) {
            lowest_base = region_base;
        }
        if (region_end > highest_end) {
            highest_end = region_end;
        }
    }
    
    dmap_phys_base = lowest_base;
    dmap_phys_max = highest_end;
    
    uart_puts("  Physical memory range: ");
    uart_puthex(dmap_phys_base);
    uart_puts(" - ");
    uart_puthex(dmap_phys_max);
    uart_puts("\n");
    
    /* Map each memory region to DMAP */
    for (int i = 0; i < mem_info->count; i++) {
        uint64_t paddr = mem_info->regions[i].base;
        size_t size = mem_info->regions[i].size;
        uint64_t vaddr = DMAP_BASE + (paddr - dmap_phys_base);
        
        uart_puts("  Mapping region ");
        uart_putc('0' + i);
        uart_puts(": PA ");
        uart_puthex(paddr);
        uart_puts(" -> VA ");
        uart_puthex(vaddr);
        uart_puts(" (");
        uart_puthex(size);
        uart_puts(" bytes)\n");
        
        /* Map with normal memory attributes */
        if (!vmm_map_range(&kernel_context, vaddr, paddr, size, 
                           VMM_ATTR_READ | VMM_ATTR_WRITE)) {
            uart_puts("VMM: Failed to create DMAP mappings for region ");
            uart_putc('0' + i);
            uart_puts("\n");
            return;
        }
    }
    
    dmap_ready = true;
    uart_puts("VMM: DMAP created successfully\n");
}

/* Debug: print page table entries for an address */
void vmm_debug_walk(vmm_context_t *ctx, uint64_t vaddr) {
    if (!ctx) {
        uart_puts("VMM: Invalid context for debug walk\n");
        return;
    }
    
    uart_puts("VMM: Page table walk for VA ");
    uart_puthex(vaddr);
    uart_puts("\n");
    
    uint64_t *table = ctx->l0_table;
    
    for (int level = ARCH_PT_TOP_LEVEL; level <= ARCH_PT_LEAF_LEVEL; level++) {
        uint64_t idx = vmm_arch_ops.get_pt_index(vaddr, level);
        uint64_t *pte = vmm_arch_ops.get_pte(table, vaddr, level);
        
        uart_puts("  L");
        uart_putc('0' + level);
        uart_puts("[");
        uart_puthex(idx);
        uart_puts("] @ ");
        uart_puthex((uint64_t)pte);
        uart_puts(" = ");
        uart_puthex(*pte);
        
        if (!vmm_arch_ops.is_pte_valid(*pte)) {
            uart_puts(" (invalid)\n");
            break;
        }
        
        if (vmm_arch_ops.is_pte_table(*pte)) {
            uart_puts(" (table)\n");
            uint64_t next_phys = vmm_arch_ops.pte_to_phys(*pte);
            table = (uint64_t*)vmm_pt_phys_to_virt(next_phys);
        } else {
            uart_puts(" (block/page)\n");
            break;
        }
    }
}