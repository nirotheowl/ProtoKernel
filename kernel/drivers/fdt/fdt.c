/*
 * kernel/drivers/fdt/fdt.c
 * 
 * Minimal Flattened Device Tree parser
 * Currently only supports memory region detection
 */

#include <drivers/fdt.h>
#include <uart.h>
#include <stdint.h>
#include <string.h>

/* Property header structure */
typedef struct {
    uint32_t len;       /* Length of property value */
    uint32_t nameoff;   /* Offset in string table */
} fdt_prop_t;

/* Helper to align to 4-byte boundary */
static inline uint32_t fdt_align(uint32_t offset) {
    return (offset + 3) & ~3;
}

/* Get pointer to strings table */
static const char *fdt_get_strings(void *fdt) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    return (const char *)fdt + fdt32_to_cpu(header->off_dt_strings);
}

/* Get property name from string table */
static const char *fdt_get_prop_name(void *fdt, uint32_t nameoff) {
    return fdt_get_strings(fdt) + nameoff;
}

/* Compare strings */
static int fdt_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* Validate FDT header */
bool fdt_valid(void *fdt) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    
    if (!fdt) {
        uart_puts("FDT: NULL pointer\n");
        return false;
    }
    
    uint32_t magic = fdt32_to_cpu(header->magic);
    if (magic != FDT_MAGIC) {
        uart_puts("FDT: Invalid magic number\n");
        return false;
    }
    
    uint32_t version = fdt32_to_cpu(header->version);
    if (version < 17) {
        uart_puts("FDT: Version too old\n");
        return false;
    }
    
    return true;
}

/* Parse memory regions from FDT */
bool fdt_get_memory(void *fdt, memory_info_t *mem_info) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    uint32_t *p;
    int depth = 0;
    bool in_memory_node = false;
    uint32_t struct_size;
    uint32_t *end;
    
    if (!fdt_valid(fdt) || !mem_info) {
        return false;
    }
    
    /* Initialize memory info */
    mem_info->count = 0;
    mem_info->total_size = 0;
    
    /* Calculate structure bounds */
    struct_size = fdt32_to_cpu(header->size_dt_struct);
    
    /* Start at structure offset */
    p = (uint32_t *)((uint8_t *)fdt + fdt32_to_cpu(header->off_dt_struct));
    end = (uint32_t *)((uint8_t *)p + struct_size);
    
    /* Walk the device tree */
    while (p < end) {
        uint32_t token = fdt32_to_cpu(*p++);
        
        switch (token) {
        case FDT_BEGIN_NODE: {
            /* Node name follows token */
            const char *name = (const char *)p;
            int len = strlen(name) + 1;
            
            /* Check if this is a memory node */
            if (depth == 1 && (fdt_strcmp(name, "memory") == 0 || 
                              fdt_strcmp(name, "memory@40000000") == 0)) {
                in_memory_node = true;
            }
            
            /* Skip name (aligned to 4 bytes) */
            p = (uint32_t *)((uint8_t *)p + fdt_align(len));
            depth++;
            break;
        }
        
        case FDT_END_NODE:
            depth--;
            if (depth == 1 && in_memory_node) {
                in_memory_node = false;
            }
            break;
            
        case FDT_PROP: {
            fdt_prop_t *prop = (fdt_prop_t *)p;
            uint32_t len = fdt32_to_cpu(prop->len);
            uint32_t nameoff = fdt32_to_cpu(prop->nameoff);
            const char *propname = fdt_get_prop_name(fdt, nameoff);
            void *propval = (uint8_t *)p + sizeof(fdt_prop_t);
            
            /* Look for "reg" property in memory node */
            if (in_memory_node && fdt_strcmp(propname, "reg") == 0) {
                /* Parse memory regions (pairs of base, size) */
                uint64_t *cells = (uint64_t *)propval;
                int num_regions = len / (2 * sizeof(uint64_t));
                
                for (int i = 0; i < num_regions && mem_info->count < 8; i++) {
                    uint64_t base = fdt64_to_cpu(cells[i * 2]);
                    uint64_t size = fdt64_to_cpu(cells[i * 2 + 1]);
                    
                    /* Only add if this looks like a reasonable memory region */
                    if (size >= 0x100000) { /* At least 1MB */
                        mem_info->regions[mem_info->count].base = base;
                        mem_info->regions[mem_info->count].size = size;
                        mem_info->total_size += size;
                        mem_info->count++;
                    }
                }
            }
            
            /* Skip property data (aligned to 4 bytes) */
            p = (uint32_t *)((uint8_t *)p + sizeof(fdt_prop_t) + fdt_align(len));
            break;
        }
        
        case FDT_NOP:
            /* Skip NOP */
            break;
            
        case FDT_END:
            /* End of device tree */
            return mem_info->count > 0;
            
        default:
            uart_puts("FDT: Unknown token: ");
            uart_puthex(token);
            uart_puts("\n");
            /* Continue parsing instead of failing on unknown tokens */
            break;
        }
    }
    
    /* If we've walked through the whole structure and found memory, that's success */
    return mem_info->count > 0;
}

/* Print memory information */
void fdt_print_memory_info(const memory_info_t *mem_info) {
    uart_puts("\nMemory regions from DTB:\n");
    uart_puts("========================\n");
    
    for (int i = 0; i < mem_info->count; i++) {
        uart_puts("  Region ");
        uart_putc('0' + i);
        uart_puts(": base=");
        uart_puthex(mem_info->regions[i].base);
        uart_puts(" size=");
        uart_puthex(mem_info->regions[i].size);
        uart_puts(" (");
        uart_puthex(mem_info->regions[i].size / (1024 * 1024));
        uart_puts(" MB)\n");
    }
    
    uart_puts("\nTotal memory: ");
    uart_puthex(mem_info->total_size);
    uart_puts(" (");
    uart_puthex(mem_info->total_size / (1024 * 1024));
    uart_puts(" MB)\n");
}

/* FDT navigation functions for libfdt compatibility */

/* Get first subnode of a node */
int fdt_first_subnode(const void *fdt, int offset) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    uint32_t struct_off = fdt32_to_cpu(header->off_dt_struct);
    uint32_t struct_size = fdt32_to_cpu(header->size_dt_struct);
    
    /* Validate offset is within bounds */
    if (offset < 0 || offset >= struct_size) {
        return -1;
    }
    
    uint32_t *p = (uint32_t *)((uint8_t *)fdt + struct_off + offset);
    uint32_t *end = (uint32_t *)((uint8_t *)fdt + struct_off + struct_size);
    int depth = -1;
    bool skip_initial = true;
    
    /* Skip to first BEGIN_NODE token at correct depth */
    while (p < end) {
        uint32_t token = fdt32_to_cpu(*p++);
        
        switch (token) {
        case FDT_BEGIN_NODE:
            if (skip_initial) {
                /* Skip the parent node itself */
                skip_initial = false;
                depth = 0;
            } else if (depth == 0) {
                /* Found first subnode */
                return (uint8_t *)p - (uint8_t *)fdt - fdt32_to_cpu(header->off_dt_struct) - 4;
            } else {
                depth++;
            }
            /* Skip node name */
            const char *name = (const char *)p;
            int len = strlen(name) + 1;
            p = (uint32_t *)((uint8_t *)p + fdt_align(len));
            break;
            
        case FDT_END_NODE:
            if (depth == 0) {
                /* No more subnodes */
                return -1;
            }
            depth--;
            break;
            
        case FDT_PROP:
            /* Skip property */
            fdt_prop_t *prop = (fdt_prop_t *)p;
            uint32_t proplen = fdt32_to_cpu(prop->len);
            p = (uint32_t *)((uint8_t *)p + sizeof(fdt_prop_t) + fdt_align(proplen));
            break;
            
        case FDT_NOP:
            /* Skip NOP */
            break;
            
        case FDT_END:
            /* End of device tree */
            return -1;
        }
    }
    
    return -1;
}

/* Get next sibling node */
int fdt_next_subnode(const void *fdt, int offset) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    uint32_t struct_off = fdt32_to_cpu(header->off_dt_struct);
    uint32_t struct_size = fdt32_to_cpu(header->size_dt_struct);
    
    /* Validate offset is within bounds */
    if (offset < 0 || offset >= struct_size) {
        return -1;
    }
    
    uint32_t *p = (uint32_t *)((uint8_t *)fdt + struct_off + offset);
    uint32_t *end = (uint32_t *)((uint8_t *)fdt + struct_off + struct_size);
    int depth = 0;
    
    /* Skip current node and find next sibling */
    while (p < end) {
        uint32_t token = fdt32_to_cpu(*p++);
        
        switch (token) {
        case FDT_BEGIN_NODE:
            depth++;
            /* Skip node name */
            const char *name = (const char *)p;
            int len = strlen(name) + 1;
            p = (uint32_t *)((uint8_t *)p + fdt_align(len));
            break;
            
        case FDT_END_NODE:
            depth--;
            if (depth == 0) {
                /* Look for next BEGIN_NODE at same level */
                while (p < end) {
                    token = fdt32_to_cpu(*p++);
                    if (token == FDT_BEGIN_NODE) {
                        return (uint8_t *)p - (uint8_t *)fdt - fdt32_to_cpu(header->off_dt_struct) - 4;
                    } else if (token == FDT_END_NODE || token == FDT_END) {
                        return -1;
                    }
                }
            }
            break;
            
        case FDT_PROP:
            /* Skip property */
            fdt_prop_t *prop = (fdt_prop_t *)p;
            uint32_t proplen = fdt32_to_cpu(prop->len);
            p = (uint32_t *)((uint8_t *)p + sizeof(fdt_prop_t) + fdt_align(proplen));
            break;
            
        case FDT_NOP:
            /* Skip NOP */
            break;
            
        case FDT_END:
            /* End of device tree */
            return -1;
        }
    }
    
    return -1;
}

/* Get node name */
const char *fdt_get_name(const void *fdt, int nodeoffset, int *len) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    uint32_t struct_off = fdt32_to_cpu(header->off_dt_struct);
    uint32_t struct_size = fdt32_to_cpu(header->size_dt_struct);
    
    /* Validate offset is within bounds */
    if (nodeoffset < 0 || nodeoffset >= struct_size) {
        return NULL;
    }
    
    uint32_t *p = (uint32_t *)((uint8_t *)fdt + struct_off + nodeoffset);
    
    /* Check we won't read past end of structure */
    if ((uint8_t *)p + sizeof(uint32_t) > (uint8_t *)fdt + struct_off + struct_size) {
        return NULL;
    }
    
    uint32_t token = fdt32_to_cpu(*p);
    if (token != FDT_BEGIN_NODE) {
        return NULL;
    }
    
    const char *name = (const char *)(p + 1);
    if (len) {
        *len = strlen(name);
    }
    
    return name;
}

/* Find subnode by name */
int fdt_subnode_offset(const void *fdt, int parentoffset, const char *name) {
    int node;
    
    fdt_for_each_subnode(node, fdt, parentoffset) {
        const char *nodename = fdt_get_name(fdt, node, NULL);
        if (nodename && fdt_strcmp(nodename, name) == 0) {
            return node;
        }
    }
    
    return -1;  /* Not found */
}