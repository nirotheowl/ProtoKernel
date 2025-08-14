/*
 * kernel/include/memory/memmap.h
 *
 * Memory region management and memory map interface
 */

#ifndef MEMMAP_H
#define MEMMAP_H

#include <stdint.h>
#include <stddef.h>

// Memory region types
typedef enum {
    MEM_TYPE_FREE,           // Available RAM
    MEM_TYPE_RESERVED,       // Reserved by system/firmware
    MEM_TYPE_KERNEL_CODE,    // Kernel code section
    MEM_TYPE_KERNEL_DATA,    // Kernel data section
    MEM_TYPE_KERNEL_BSS,     // Kernel BSS section
    MEM_TYPE_DEVICE_MMIO,    // Memory-mapped I/O
    MEM_TYPE_FRAMEBUFFER,    // Graphics framebuffer
    MEM_TYPE_ACPI_RECLAIM,   // ACPI tables (can be reclaimed)
    MEM_TYPE_ACPI_NVS,       // ACPI non-volatile storage
    MEM_TYPE_BOOT_DATA,      // Boot loader data (can be reclaimed)
    MEM_TYPE_DMA_COHERENT,   // DMA coherent pool
    MEM_TYPE_PAGE_TABLES,    // Page table storage
    MEM_TYPE_SECURE,         // Secure/TrustZone memory
} mem_type_t;

// Memory region attributes
typedef enum {
    MEM_ATTR_NONE        = 0x00,
    MEM_ATTR_CACHEABLE   = 0x01,
    MEM_ATTR_WRITE_BACK  = 0x02,
    MEM_ATTR_WRITE_THRU  = 0x04,
    MEM_ATTR_WRITE_COMB  = 0x08,
    MEM_ATTR_EXECUTABLE  = 0x10,
    MEM_ATTR_DMA_CAPABLE = 0x20,
    MEM_ATTR_SECURE      = 0x40,
} mem_attr_t;

// Memory region descriptor
typedef struct mem_region {
    uint64_t base;
    uint64_t size;
    mem_type_t type;
    mem_attr_t attrs;
    const char* name;
    struct mem_region* next;
} mem_region_t;

// Memory map manager functions
void memmap_init(void);
void memmap_add_region(uint64_t base, uint64_t size, mem_type_t type, 
                      mem_attr_t attrs, const char* name);
void memmap_remove_region(uint64_t base);
mem_region_t* memmap_find_region(uint64_t addr);
mem_region_t* memmap_get_regions(void);

// Device tree memory parsing
int memmap_parse_device_tree(void* dtb);

// Get memory type name
const char* memmap_type_name(mem_type_t type);

// Check if address is in specific type of memory
int memmap_is_device_memory(uint64_t addr);
int memmap_is_secure_memory(uint64_t addr);
int memmap_is_dma_capable(uint64_t addr);

#endif // MEMMAP_H