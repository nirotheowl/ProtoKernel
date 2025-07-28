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
        // uart_puts("FDT: NULL pointer\n");
        return false;
    }
    
    uint32_t magic = fdt32_to_cpu(header->magic);
    if (magic != FDT_MAGIC) {
        // uart_puts("FDT: Invalid magic number\n");
        return false;
    }
    
    uint32_t version = fdt32_to_cpu(header->version);
    if (version < 17) {
        // uart_puts("FDT: Version too old\n");
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
            // uart_puts("FDT: Unknown token: ");
            // uart_puthex(token);
            // uart_puts("\n");
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
    
    // uart_puts("FDT: fdt_first_subnode called with offset=");
    // uart_puthex(offset);
    // uart_puts("\n");
    
    /* Validate offset is within bounds */
    if (offset < 0 || offset >= struct_size) {
        // uart_puts("FDT: fdt_first_subnode - offset out of bounds\n");
        return -1;
    }
    
    uint32_t *p = (uint32_t *)((uint8_t *)fdt + struct_off + offset);
    uint32_t *end = (uint32_t *)((uint8_t *)fdt + struct_off + struct_size);
    int depth = -1;
    bool skip_initial = true;
    
    // uart_puts("FDT: fdt_first_subnode - starting scan at p=");
    // uart_puthex((uint64_t)p);
    // uart_puts(", end=");
    // uart_puthex((uint64_t)end);
    // uart_puts("\n");
    
    /* Skip to first BEGIN_NODE token at correct depth */
    while (p < end) {
        uint32_t token = fdt32_to_cpu(*p++);
        
        // uart_puts("FDT: fdt_first_subnode - token=");
        // uart_puthex(token);
        // uart_puts(" at ");
        // uart_puthex((uint64_t)(p-1));
        // uart_puts("\n");
        
        switch (token) {
        case FDT_BEGIN_NODE:
            if (skip_initial) {
                /* Skip the parent node itself */
                skip_initial = false;
                depth = 0;
                // uart_puts("FDT: fdt_first_subnode - skipping parent node\n");
            } else if (depth == 0) {
                /* Found first subnode */
                int result = (uint8_t *)p - (uint8_t *)fdt - fdt32_to_cpu(header->off_dt_struct) - 4;
                // uart_puts("FDT: fdt_first_subnode - found subnode at offset ");
                // uart_puthex(result);
                // uart_puts("\n");
                return result;
            } else {
                depth++;
            }
            /* Skip node name */
            const char *name = (const char *)p;
            int len = strlen(name) + 1;
            // uart_puts("FDT: fdt_first_subnode - node name='");
            // uart_puts(name);
            // uart_puts("', len=");
            // uart_puthex(len);
            // uart_puts("\n");
            p = (uint32_t *)((uint8_t *)p + fdt_align(len));
            break;
            
        case FDT_END_NODE:
            // uart_puts("FDT: fdt_first_subnode - END_NODE, depth=");
            // uart_puthex(depth);
            // uart_puts("\n");
            if (depth == 0) {
                /* No more subnodes */
                // uart_puts("FDT: fdt_first_subnode - no more subnodes\n");
                return -1;
            }
            depth--;
            break;
            
        case FDT_PROP:
            /* Skip property */
            fdt_prop_t *prop = (fdt_prop_t *)p;
            uint32_t proplen = fdt32_to_cpu(prop->len);
            // uart_puts("FDT: fdt_first_subnode - skipping property, len=");
            // uart_puthex(proplen);
            // uart_puts("\n");
            p = (uint32_t *)((uint8_t *)p + sizeof(fdt_prop_t) + fdt_align(proplen));
            break;
            
        case FDT_NOP:
            /* Skip NOP */
            // uart_puts("FDT: fdt_first_subnode - NOP\n");
            break;
            
        case FDT_END:
            /* End of device tree */
            // uart_puts("FDT: fdt_first_subnode - END of tree\n");
            return -1;
        }
    }
    
    // uart_puts("FDT: fdt_first_subnode - reached end of structure\n");
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
    
    // uart_puts("FDT: fdt_get_name called with nodeoffset=");
    // uart_puthex(nodeoffset);
    // uart_puts(", struct_off=");
    // uart_puthex(struct_off);
    // uart_puts(", struct_size=");
    // uart_puthex(struct_size);
    // uart_puts("\n");
    
    /* Validate offset is within bounds */
    if (nodeoffset < 0 || nodeoffset >= struct_size) {
        // uart_puts("FDT: fdt_get_name - offset out of bounds\n");
        return NULL;
    }
    
    uint32_t *p = (uint32_t *)((uint8_t *)fdt + struct_off + nodeoffset);
    
    // uart_puts("FDT: fdt_get_name - p=");
    // uart_puthex((uint64_t)p);
    // uart_puts("\n");
    
    /* Check we won't read past end of structure */
    if ((uint8_t *)p + sizeof(uint32_t) > (uint8_t *)fdt + struct_off + struct_size) {
        // uart_puts("FDT: fdt_get_name - would read past end\n");
        return NULL;
    }
    
    uint32_t token = fdt32_to_cpu(*p);
    // uart_puts("FDT: fdt_get_name - token=");
    // uart_puthex(token);
    // uart_puts(" (expected BEGIN_NODE=");
    // uart_puthex(FDT_BEGIN_NODE);
    // uart_puts(")\n");
    
    if (token != FDT_BEGIN_NODE) {
        // uart_puts("FDT: fdt_get_name - not a BEGIN_NODE\n");
        return NULL;
    }
    
    const char *name = (const char *)(p + 1);
    // uart_puts("FDT: fdt_get_name - name='");
    // uart_puts(name);
    // uart_puts("'\n");
    
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

/* Check FDT header validity */
int fdt_check_header(const void *fdt) {
    const fdt_header_t *header = (const fdt_header_t *)fdt;
    
    if (!fdt) {
        return -1;
    }
    
    if (fdt32_to_cpu(header->magic) != FDT_MAGIC) {
        return -1;
    }
    
    /* Check version */
    uint32_t version = fdt32_to_cpu(header->version);
    if (version < 17) {
        return -1;
    }
    
    /* Check structure size is reasonable */
    uint32_t totalsize = fdt32_to_cpu(header->totalsize);
    if (totalsize < sizeof(fdt_header_t) || totalsize > 0x100000) { /* Max 1MB */
        return -1;
    }
    
    return 0;
}

/* Get property value from node */
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    uint32_t struct_off = fdt32_to_cpu(header->off_dt_struct);
    uint32_t struct_size = fdt32_to_cpu(header->size_dt_struct);
    
    if (lenp) {
        *lenp = 0;
    }
    
    /* Validate offset */
    if (nodeoffset < 0 || nodeoffset >= struct_size || !name) {
        return NULL;
    }
    
    uint32_t *p = (uint32_t *)((uint8_t *)fdt + struct_off + nodeoffset);
    uint32_t *end = (uint32_t *)((uint8_t *)fdt + struct_off + struct_size);
    int depth = 0;
    bool in_target_node = false;
    
    /* First, verify we're at a BEGIN_NODE */
    if (fdt32_to_cpu(*p) == FDT_BEGIN_NODE) {
        in_target_node = true;
        p++; /* Skip BEGIN_NODE token */
        /* Skip node name */
        const char *nodename = (const char *)p;
        int namelen = strlen(nodename) + 1;
        p = (uint32_t *)((uint8_t *)p + fdt_align(namelen));
    }
    
    /* Search for property in this node */
    while (p < end && in_target_node) {
        uint32_t token = fdt32_to_cpu(*p++);
        
        switch (token) {
        case FDT_BEGIN_NODE:
            /* Skip child nodes */
            depth++;
            const char *childname = (const char *)p;
            int childlen = strlen(childname) + 1;
            p = (uint32_t *)((uint8_t *)p + fdt_align(childlen));
            break;
            
        case FDT_END_NODE:
            if (depth == 0) {
                /* End of target node */
                return NULL;
            }
            depth--;
            break;
            
        case FDT_PROP:
            if (depth == 0) {
                /* Property in target node */
                fdt_prop_t *prop = (fdt_prop_t *)p;
                uint32_t proplen = fdt32_to_cpu(prop->len);
                uint32_t nameoff = fdt32_to_cpu(prop->nameoff);
                const char *propname = fdt_get_prop_name((void *)fdt, nameoff);
                
                if (fdt_strcmp(propname, name) == 0) {
                    /* Found the property */
                    if (lenp) {
                        *lenp = proplen;
                    }
                    return (uint8_t *)p + sizeof(fdt_prop_t);
                }
            }
            /* Skip property data */
            fdt_prop_t *prop = (fdt_prop_t *)p;
            uint32_t proplen = fdt32_to_cpu(prop->len);
            p = (uint32_t *)((uint8_t *)p + sizeof(fdt_prop_t) + fdt_align(proplen));
            break;
            
        case FDT_NOP:
            break;
            
        case FDT_END:
            return NULL;
        }
    }
    
    return NULL;
}

/* Get parent node offset */
int fdt_parent_offset(const void *fdt, int nodeoffset) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    uint32_t struct_off = fdt32_to_cpu(header->off_dt_struct);
    uint32_t struct_size = fdt32_to_cpu(header->size_dt_struct);
    
    if (nodeoffset <= 0) {
        return -1; /* Root has no parent */
    }
    
    /* We need to traverse from the beginning to find parent */
    uint32_t *p = (uint32_t *)((uint8_t *)fdt + struct_off);
    uint32_t *end = (uint32_t *)((uint8_t *)fdt + struct_off + struct_size);
    uint32_t *target = (uint32_t *)((uint8_t *)fdt + struct_off + nodeoffset);
    
    int parent_offset = 0;
    int current_offset = 0;
    int depth = 0;
    
    while (p < end && p < target) {
        uint32_t token = fdt32_to_cpu(*p++);
        current_offset = (uint8_t *)(p - 1) - (uint8_t *)fdt - struct_off;
        
        switch (token) {
        case FDT_BEGIN_NODE:
            if (p - 1 == target) {
                /* Found target node, return last parent */
                return parent_offset;
            }
            if (depth == 0) {
                parent_offset = current_offset;
            }
            depth++;
            /* Skip node name */
            const char *name = (const char *)p;
            int len = strlen(name) + 1;
            p = (uint32_t *)((uint8_t *)p + fdt_align(len));
            break;
            
        case FDT_END_NODE:
            depth--;
            break;
            
        case FDT_PROP:
            /* Skip property */
            fdt_prop_t *prop = (fdt_prop_t *)p;
            uint32_t proplen = fdt32_to_cpu(prop->len);
            p = (uint32_t *)((uint8_t *)p + sizeof(fdt_prop_t) + fdt_align(proplen));
            break;
            
        case FDT_NOP:
            break;
            
        case FDT_END:
            return -1;
        }
    }
    
    return -1;
}

/* Get next node in depth-first traversal */
int fdt_next_node(const void *fdt, int offset, int *depth) {
    fdt_header_t *header = (fdt_header_t *)fdt;
    uint32_t struct_off = fdt32_to_cpu(header->off_dt_struct);
    uint32_t struct_size = fdt32_to_cpu(header->size_dt_struct);
    uint32_t *p, *end;
    int cur_depth = 0;
    
    if (offset < 0) {
        /* Start from beginning */
        p = (uint32_t *)((uint8_t *)fdt + struct_off);
    } else {
        /* Continue from offset */
        p = (uint32_t *)((uint8_t *)fdt + struct_off + offset);
        /* Skip current node */
        if (fdt32_to_cpu(*p) == FDT_BEGIN_NODE) {
            p++;
            const char *name = (const char *)p;
            int len = strlen(name) + 1;
            p = (uint32_t *)((uint8_t *)p + fdt_align(len));
        }
    }
    
    end = (uint32_t *)((uint8_t *)fdt + struct_off + struct_size);
    
    while (p < end) {
        uint32_t token = fdt32_to_cpu(*p++);
        int node_offset = (uint8_t *)(p - 1) - (uint8_t *)fdt - struct_off;
        
        switch (token) {
        case FDT_BEGIN_NODE:
            if (depth) {
                *depth = cur_depth;
            }
            return node_offset;
            
        case FDT_END_NODE:
            cur_depth--;
            if (cur_depth < 0 && depth) {
                *depth = cur_depth;
            }
            break;
            
        case FDT_PROP:
            /* Skip property */
            fdt_prop_t *prop = (fdt_prop_t *)p;
            uint32_t proplen = fdt32_to_cpu(prop->len);
            p = (uint32_t *)((uint8_t *)p + sizeof(fdt_prop_t) + fdt_align(proplen));
            break;
            
        case FDT_NOP:
            break;
            
        case FDT_END:
            return -1;
        }
    }
    
    return -1;
}

/* Get node offset by path */
int fdt_path_offset(const void *fdt, const char *path) {
    if (!path || path[0] != '/') {
        return -1;
    }
    
    /* Handle root node */
    if (path[1] == '\0') {
        return 0;
    }
    
    /* For now, we only support simple paths without full parsing */
    /* This would need a complete implementation to parse paths like /memory@40000000 */
    int offset = 0;
    const char *p = path + 1; /* Skip leading '/' */
    const char *end;
    
    while (*p) {
        /* Find next component */
        end = p;
        while (*end && *end != '/') {
            end++;
        }
        
        /* Create null-terminated component name */
        char component[64];
        int len = end - p;
        if (len >= 64) {
            return -1;
        }
        
        int i;
        for (i = 0; i < len; i++) {
            component[i] = p[i];
        }
        component[i] = '\0';
        
        /* Find subnode */
        offset = fdt_subnode_offset(fdt, offset, component);
        if (offset < 0) {
            return -1;
        }
        
        /* Move to next component */
        p = end;
        if (*p == '/') {
            p++;
        }
    }
    
    return offset;
}