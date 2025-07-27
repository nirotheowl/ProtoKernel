/*
 * kernel/include/drivers/fdt.h
 * 
 * Flattened Device Tree (FDT) parser interface
 * Minimal implementation for early boot memory detection
 */

#ifndef _FDT_H_
#define _FDT_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* FDT standard header */
typedef struct {
    uint32_t magic;              /* 0xd00dfeed */
    uint32_t totalsize;          /* Total size of DT block */
    uint32_t off_dt_struct;      /* Offset to structure */
    uint32_t off_dt_strings;     /* Offset to strings */
    uint32_t off_mem_rsvmap;     /* Offset to memory reserve map */
    uint32_t version;            /* Format version */
    uint32_t last_comp_version;  /* Last compatible version */
    uint32_t boot_cpuid_phys;    /* Physical CPU id we're booting on */
    uint32_t size_dt_strings;    /* Size of the strings block */
    uint32_t size_dt_struct;     /* Size of the structure block */
} fdt_header_t;

/* FDT tokens */
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

#define FDT_MAGIC       0xd00dfeed

/* Memory region info */
typedef struct {
    uint64_t base;
    uint64_t size;
} memory_region_t;

/* Memory info structure */
typedef struct {
    memory_region_t regions[8];  /* Support up to 8 memory regions */
    int count;                   /* Number of regions found */
    uint64_t total_size;         /* Total memory size */
} memory_info_t;

/* Function prototypes */
bool fdt_valid(void *fdt);
bool fdt_get_memory(void *fdt, memory_info_t *mem_info);
void fdt_print_memory_info(const memory_info_t *mem_info);

/* Helper macros for big-endian to little-endian conversion */
#define fdt32_to_cpu(x) __builtin_bswap32(x)
#define fdt64_to_cpu(x) __builtin_bswap64(x)

/* FDT node iteration macros (libfdt compatibility) */
#define fdt_for_each_subnode(node, fdt, parent)    \
    for (node = fdt_first_subnode(fdt, parent);   \
         node >= 0;                                \
         node = fdt_next_subnode(fdt, node))

/* FDT navigation functions */
int fdt_first_subnode(const void *fdt, int offset);
int fdt_next_subnode(const void *fdt, int offset);
const char *fdt_get_name(const void *fdt, int nodeoffset, int *len);
int fdt_subnode_offset(const void *fdt, int parentoffset, const char *name);

#endif /* _FDT_H_ */