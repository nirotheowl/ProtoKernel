/*
 * kernel/include/device/resource.h
 * 
 * Device resource management
 * Handles memory regions, interrupts, DMA channels, etc.
 */

#ifndef __RESOURCE_H
#define __RESOURCE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
struct device;

/* Resource types */
typedef enum {
    RES_TYPE_MEM,           /* Memory-mapped I/O */
    RES_TYPE_IO,            /* I/O port (not used on ARM64) */
    RES_TYPE_IRQ,           /* Interrupt */
    RES_TYPE_DMA,           /* DMA channel */
    RES_TYPE_BUS,           /* Bus number range */
    RES_TYPE_CLOCK,         /* Clock resource */
    RES_TYPE_POWER,         /* Power domain */
    RES_TYPE_RESET,         /* Reset line */
    RES_TYPE_MAX
} resource_type_t;

/* Resource structure */
struct resource {
    resource_type_t     type;       /* Resource type */
    uint64_t            start;      /* Start address/number */
    uint64_t            end;        /* End address/number (inclusive) */
    uint64_t            flags;      /* Resource-specific flags */
    const char          *name;      /* Resource name */
    void                *mapped_addr; /* Virtual base address if mapped */
    struct resource     *parent;    /* Parent resource (for hierarchical resources) */
    struct resource     *child;     /* First child resource */
    struct resource     *sibling;   /* Next sibling resource */
};

/* Memory resource flags */
#define RES_MEM_CACHEABLE       (1 << 0)   /* Memory is cacheable */
#define RES_MEM_PREFETCH        (1 << 1)   /* Memory supports prefetch */
#define RES_MEM_WRITETHROUGH    (1 << 2)   /* Write-through caching */
#define RES_MEM_WRITECOMBINE    (1 << 3)   /* Write-combining allowed */
#define RES_MEM_NONPOSTED       (1 << 4)   /* Non-posted writes */
#define RES_MEM_32BIT           (1 << 5)   /* 32-bit addressable only */
#define RES_MEM_64BIT           (1 << 6)   /* 64-bit addressable */

/* Interrupt resource flags */
#define RES_IRQ_EDGE            (1 << 0)   /* Edge-triggered */
#define RES_IRQ_LEVEL           (1 << 1)   /* Level-triggered */
#define RES_IRQ_HIGHLEVEL       (1 << 2)   /* Active high level */
#define RES_IRQ_LOWLEVEL        (1 << 3)   /* Active low level */
#define RES_IRQ_RISING          (1 << 4)   /* Rising edge */
#define RES_IRQ_FALLING         (1 << 5)   /* Falling edge */
#define RES_IRQ_SHARED          (1 << 6)   /* Can be shared */
#define RES_IRQ_WAKE            (1 << 7)   /* Can wake system */

/* DMA resource flags */
#define RES_DMA_32BIT           (1 << 0)   /* 32-bit DMA only */
#define RES_DMA_64BIT           (1 << 1)   /* 64-bit DMA capable */
#define RES_DMA_COHERENT        (1 << 2)   /* Cache-coherent DMA */

/* Resource allocation flags */
#define RES_ALLOC_ANYWHERE      (1 << 0)   /* Allocate anywhere in range */
#define RES_ALLOC_ALIGN         (1 << 1)   /* Align to size */
#define RES_ALLOC_FIXED         (1 << 2)   /* Fixed address required */

/* Resource API functions */

/* Resource allocation and management */
struct resource *resource_alloc(void);
void resource_free(struct resource *res);
bool resource_init(struct resource *res, resource_type_t type,
                  uint64_t start, uint64_t end, const char *name);

/* Device resource management */
int device_add_resource(struct device *dev, struct resource *res);
int device_add_mem_resource(struct device *dev, uint64_t start, uint64_t size,
                           uint64_t flags, const char *name);
int device_add_irq_resource(struct device *dev, uint32_t irq, uint64_t flags,
                           const char *name);
struct resource *device_get_resource(struct device *dev, resource_type_t type,
                                   int index);
struct resource *device_get_resource_by_name(struct device *dev,
                                           resource_type_t type,
                                           const char *name);

/* Resource queries */
uint64_t resource_size(const struct resource *res);
bool resource_contains(const struct resource *parent, const struct resource *child);
bool resource_overlaps(const struct resource *r1, const struct resource *r2);
bool resource_is_valid(const struct resource *res);

/* Resource tree management */
int resource_add_child(struct resource *parent, struct resource *child);
int resource_remove_child(struct resource *parent, struct resource *child);
struct resource *resource_find_child(struct resource *parent, uint64_t start,
                                   uint64_t end);

/* Resource allocation within ranges */
int resource_allocate_range(struct resource *root, struct resource *new_res,
                          uint64_t size, uint64_t align, uint64_t flags);
int resource_release_range(struct resource *root, uint64_t start, uint64_t size);

/* Memory-specific helpers */
/* Note: __iomem is Linux-specific, we'll use void* for now */
void *resource_ioremap(struct resource *res);
void resource_iounmap(void *addr);
bool resource_is_mmio(const struct resource *res);
void *resource_get_mapped_addr(struct resource *res);
int resource_set_mapped_addr(struct resource *res, void *vaddr);

/* IRQ-specific helpers */
bool resource_is_irq_valid(const struct resource *res);
uint32_t resource_irq_get_number(const struct resource *res);
uint64_t resource_irq_get_flags(const struct resource *res);

/* Debug functions */
void resource_print(const struct resource *res);
void resource_print_tree(const struct resource *root, int indent);
const char *resource_type_to_string(resource_type_t type);

/* Resource iterators */
#define resource_for_each_child(child, parent) \
    for ((child) = (parent)->child; (child); (child) = (child)->sibling)

#define device_for_each_resource(res, dev, type) \
    for (int _i = 0; ((res) = device_get_resource((dev), (type), _i)); _i++)

/* Common resource definitions */
extern struct resource system_mem_resource;     /* System RAM */
extern struct resource system_io_resource;      /* System I/O space */
extern struct resource pci_mem_resource;        /* PCI memory space */
extern struct resource pci_io_resource;         /* PCI I/O space */

#endif /* __RESOURCE_H */