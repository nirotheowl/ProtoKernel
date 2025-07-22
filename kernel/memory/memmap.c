#include <memory/memmap.h>
#include <memory/paging.h>
#include <uart.h>
#include <stddef.h>

// Head of the memory region linked list
static mem_region_t* region_list = NULL;

// Static pool for initial regions (before PMM is ready)
#define STATIC_REGION_COUNT 32
static mem_region_t static_regions[STATIC_REGION_COUNT];
static int static_region_index = 0;

// Memory type names for debugging
static const char* mem_type_names[] = {
    [MEM_TYPE_FREE]          = "Free RAM",
    [MEM_TYPE_RESERVED]      = "Reserved",
    [MEM_TYPE_KERNEL_CODE]   = "Kernel Code",
    [MEM_TYPE_KERNEL_DATA]   = "Kernel Data",
    [MEM_TYPE_KERNEL_BSS]    = "Kernel BSS",
    [MEM_TYPE_DEVICE_MMIO]   = "Device MMIO",
    [MEM_TYPE_FRAMEBUFFER]   = "Framebuffer",
    [MEM_TYPE_ACPI_RECLAIM]  = "ACPI Reclaim",
    [MEM_TYPE_ACPI_NVS]      = "ACPI NVS",
    [MEM_TYPE_BOOT_DATA]     = "Boot Data",
    [MEM_TYPE_DMA_COHERENT]  = "DMA Coherent",
    [MEM_TYPE_PAGE_TABLES]   = "Page Tables",
    [MEM_TYPE_SECURE]        = "Secure",
};

// Initialize memory map manager
void memmap_init(void) {
    region_list = NULL;
    static_region_index = 0;
    
    // Get kernel addresses
    extern char __text_start, __text_end;
    extern char __data_start, __data_end;
    extern char __bss_start, __bss_end;
    
    // Add kernel regions
    memmap_add_region((uint64_t)&__text_start, 
                     (uint64_t)&__text_end - (uint64_t)&__text_start,
                     MEM_TYPE_KERNEL_CODE,
                     MEM_ATTR_CACHEABLE | MEM_ATTR_WRITE_BACK | MEM_ATTR_EXECUTABLE,
                     "Kernel .text");
    
    memmap_add_region((uint64_t)&__data_start,
                     (uint64_t)&__data_end - (uint64_t)&__data_start,
                     MEM_TYPE_KERNEL_DATA,
                     MEM_ATTR_CACHEABLE | MEM_ATTR_WRITE_BACK,
                     "Kernel .data");
    
    memmap_add_region((uint64_t)&__bss_start,
                     (uint64_t)&__bss_end - (uint64_t)&__bss_start,
                     MEM_TYPE_KERNEL_BSS,
                     MEM_ATTR_CACHEABLE | MEM_ATTR_WRITE_BACK,
                     "Kernel .bss");
    
    // Add known device regions
    memmap_add_region(0x09000000, 0x1000,
                     MEM_TYPE_DEVICE_MMIO,
                     MEM_ATTR_NONE,
                     "PL011 UART");
    
    // Add typical ARM device regions (will be refined by device tree later)
    memmap_add_region(0x08000000, 0x10000,
                     MEM_TYPE_DEVICE_MMIO,
                     MEM_ATTR_NONE,
                     "GIC Distributor");
    
    memmap_add_region(0x08010000, 0x10000,
                     MEM_TYPE_DEVICE_MMIO,
                     MEM_ATTR_NONE,
                     "GIC CPU Interface");
    
    // PCI MMIO region (typical for virt machine)
    memmap_add_region(0x10000000, 0x2f000000,
                     MEM_TYPE_DEVICE_MMIO,
                     MEM_ATTR_NONE,
                     "PCI MMIO");
}

// Add a memory region
void memmap_add_region(uint64_t base, uint64_t size, mem_type_t type,
                      mem_attr_t attrs, const char* name) {
    mem_region_t* region = NULL;
    
    // Use static allocation if available
    if (static_region_index < STATIC_REGION_COUNT) {
        region = &static_regions[static_region_index++];
    } else {
        // Would use dynamic allocation here if PMM is ready
        return;  // Out of static regions
    }
    
    region->base = base;
    region->size = size;
    region->type = type;
    region->attrs = attrs;
    region->name = name;
    region->next = NULL;
    
    // Insert sorted by base address
    if (!region_list || region_list->base > base) {
        region->next = region_list;
        region_list = region;
    } else {
        mem_region_t* curr = region_list;
        while (curr->next && curr->next->base < base) {
            curr = curr->next;
        }
        region->next = curr->next;
        curr->next = region;
    }
}

// Find region containing address
mem_region_t* memmap_find_region(uint64_t addr) {
    mem_region_t* region = region_list;
    while (region) {
        if (addr >= region->base && addr < region->base + region->size) {
            return region;
        }
        region = region->next;
    }
    return NULL;
}

// Get region list
mem_region_t* memmap_get_regions(void) {
    return region_list;
}


// Get memory type name
const char* memmap_type_name(mem_type_t type) {
    if (type < sizeof(mem_type_names) / sizeof(mem_type_names[0])) {
        return mem_type_names[type];
    }
    return "Unknown";
}

// Check if address is device memory
int memmap_is_device_memory(uint64_t addr) {
    mem_region_t* region = memmap_find_region(addr);
    return region && region->type == MEM_TYPE_DEVICE_MMIO;
}

// Check if address is secure memory
int memmap_is_secure_memory(uint64_t addr) {
    mem_region_t* region = memmap_find_region(addr);
    return region && (region->type == MEM_TYPE_SECURE || 
                     (region->attrs & MEM_ATTR_SECURE));
}

// Check if address is DMA capable
int memmap_is_dma_capable(uint64_t addr) {
    mem_region_t* region = memmap_find_region(addr);
    return region && (region->type == MEM_TYPE_DMA_COHERENT ||
                     (region->attrs & MEM_ATTR_DMA_CAPABLE));
}

// Get appropriate page attributes for region
uint64_t memmap_get_page_attrs(uint64_t addr) {
    mem_region_t* region = memmap_find_region(addr);
    if (!region) {
        // Default to normal cached memory
        return PTE_KERNEL_BLOCK;
    }
    
    uint64_t attrs = PTE_AF | PTE_VALID;
    
    // Set memory type based on region
    switch (region->type) {
        case MEM_TYPE_DEVICE_MMIO:
            attrs |= (MT_DEVICE_nGnRnE << 2);
            attrs |= PTE_NS;
            break;
            
        case MEM_TYPE_FRAMEBUFFER:
            attrs |= (MT_DEVICE_nGnRE << 2);  // Allow early ack for framebuffer
            attrs |= PTE_NS;
            break;
            
        case MEM_TYPE_DMA_COHERENT:
            attrs |= (MT_NORMAL_NC << 2);     // Non-cacheable for DMA
            break;
            
        default:
            // Normal memory
            if (region->attrs & MEM_ATTR_CACHEABLE) {
                if (region->attrs & MEM_ATTR_WRITE_BACK) {
                    attrs |= (MT_NORMAL << 2);
                } else if (region->attrs & MEM_ATTR_WRITE_THRU) {
                    attrs |= (MT_NORMAL_NC << 2);  // ARM doesn't have WT, use NC
                }
            } else {
                attrs |= (MT_NORMAL_NC << 2);
            }
            break;
    }
    
    // Set access permissions
    if (region->type == MEM_TYPE_KERNEL_CODE) {
        // Read-only executable: use read-only permission
        attrs |= PTE_AP_RO;
        // Don't override memory type for device memory
        if (region->type != MEM_TYPE_DEVICE_MMIO && region->type != MEM_TYPE_FRAMEBUFFER) {
            attrs |= (MT_NORMAL << 2);  // Normal memory
            attrs |= PTE_SH_INNER;      // Inner shareable
        }
        // Don't set UXN/PXN for executable code
    } else if (region->attrs & MEM_ATTR_EXECUTABLE) {
        // Read-write executable
        attrs |= PTE_AP_RW;
        if (region->type != MEM_TYPE_DEVICE_MMIO && region->type != MEM_TYPE_FRAMEBUFFER) {
            attrs |= (MT_NORMAL << 2);
            attrs |= PTE_SH_INNER;
        }
    } else {
        // Read-write no-execute (default)
        attrs |= PTE_AP_RW;
        if (region->type != MEM_TYPE_DEVICE_MMIO && region->type != MEM_TYPE_FRAMEBUFFER) {
            attrs |= PTE_SH_INNER;
        }
        attrs |= PTE_UXN | PTE_PXN;  // No execute
    }
    
    // Handle secure memory
    if (!(region->attrs & MEM_ATTR_SECURE)) {
        attrs |= PTE_NS;
    }
    
    return attrs;
}

// Parse device tree for memory regions (stub for now)
int memmap_parse_device_tree(void* dtb) {
    (void)dtb; // Suppress unused parameter warning
    // TODO: Implement device tree parsing
    return 0;
}