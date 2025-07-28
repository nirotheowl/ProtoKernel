/*
 * kernel/include/device/device_tree.h
 * 
 * FDT to device conversion utilities
 * Bridges between FDT parsing and device management
 */

#ifndef __DEVICE_TREE_H
#define __DEVICE_TREE_H

#include <device/device.h>
#include <device/resource.h>
#include <stdint.h>
#include <stdbool.h>

/* FDT property names */
#define FDT_PROP_COMPATIBLE     "compatible"
#define FDT_PROP_REG            "reg"
#define FDT_PROP_INTERRUPTS     "interrupts"
#define FDT_PROP_INTERRUPT_PARENT "interrupt-parent"
#define FDT_PROP_CLOCKS         "clocks"
#define FDT_PROP_CLOCK_NAMES    "clock-names"
#define FDT_PROP_STATUS         "status"
#define FDT_PROP_PHANDLE        "phandle"
#define FDT_PROP_NAME           "name"
#define FDT_PROP_DEVICE_TYPE    "device_type"
#define FDT_PROP_RANGES         "ranges"
#define FDT_PROP_DMA_RANGES     "dma-ranges"
#define FDT_PROP_ADDRESS_CELLS  "#address-cells"
#define FDT_PROP_SIZE_CELLS     "#size-cells"

/* Device status values */
#define FDT_STATUS_OKAY         "okay"
#define FDT_STATUS_DISABLED     "disabled"
#define FDT_STATUS_FAIL         "fail"

/* Early device allocation context */
struct dt_early_context {
    void                *pool_base;     /* Early memory pool base */
    size_t              pool_size;      /* Pool size */
    size_t              pool_used;      /* Current usage */
    uint32_t            device_count;   /* Number of devices allocated */
};

/* Device tree parsing callbacks */
typedef int (*dt_device_callback_t)(int node_offset, const char *name,
                                   const char *path, void *ctx);

/* FDT to device conversion API */

/* Initialize device tree subsystem */
int device_tree_init(void *fdt_blob);

/* Early boot device allocation */
struct device *device_tree_alloc_early(struct dt_early_context *ctx);
int device_tree_init_early_pool(void *pool_base, size_t pool_size);
size_t device_tree_get_early_pool_usage(void);

/* Device enumeration and creation */
int device_tree_enumerate(dt_device_callback_t callback, void *ctx);
struct device *device_tree_create_device(int node_offset);
int device_tree_populate_devices(void);

/* Property parsing */
int device_tree_parse_reg(struct device *dev, int node_offset);
int device_tree_parse_interrupts(struct device *dev, int node_offset);
int device_tree_parse_clocks(struct device *dev, int node_offset);
bool device_tree_is_device_enabled(int node_offset);

/* Resource extraction */
int device_tree_get_reg_count(int node_offset);
int device_tree_get_interrupt_count(int node_offset);
bool device_tree_get_reg_by_index(int node_offset, int index,
                                 uint64_t *addr, uint64_t *size);
bool device_tree_get_interrupt_by_index(int node_offset, int index,
                                       uint32_t *irq, uint32_t *flags);

/* Compatible string handling */
const char *device_tree_get_compatible(int node_offset);
bool device_tree_is_compatible(int node_offset, const char *compatible);
int device_tree_find_compatible_node(const char *compatible, int start_offset);

/* Device type determination */
device_type_t device_tree_get_device_type(int node_offset);
device_type_t device_tree_compatible_to_type(const char *compatible);

/* Address translation */
uint64_t device_tree_translate_address(int node_offset, uint64_t addr);
bool device_tree_get_dma_range(int node_offset, uint64_t *cpu_addr,
                              uint64_t *dma_addr, uint64_t *size);

/* Phandle resolution */
int device_tree_get_phandle(int node_offset);
int device_tree_find_by_phandle(uint32_t phandle);
struct device *device_tree_get_device_by_phandle(uint32_t phandle);

/* Interrupt parent handling */
int device_tree_get_interrupt_parent(int node_offset);
bool device_tree_translate_interrupt(int node_offset, uint32_t intspec,
                                   uint32_t *irq, uint32_t *flags);

/* Path and name utilities */
const char *device_tree_get_node_name(int node_offset);
const char *device_tree_get_node_path(int node_offset);
int device_tree_find_node_by_path(const char *path);

/* Debug helpers */
void device_tree_print_node(int node_offset, int depth);
void device_tree_dump_devices(void);

/* Common compatible string patterns */
#define DT_COMPAT_ARM_CORTEX_A      "arm,cortex-a"
#define DT_COMPAT_ARM_GIC_V3        "arm,gic-v3"
#define DT_COMPAT_ARM_GIC_400       "arm,gic-400"
#define DT_COMPAT_ARM_TIMER         "arm,armv8-timer"
#define DT_COMPAT_ARM_PL011         "arm,pl011"
#define DT_COMPAT_PCI_HOST_ECAM     "pci-host-ecam-generic"
#define DT_COMPAT_VIRTIO_MMIO       "virtio,mmio"
#define DT_COMPAT_SIMPLE_BUS        "simple-bus"

/* Helper macros */
#define device_tree_for_each_node(node, fdt) \
    for ((node) = fdt_next_node((fdt), -1, NULL); \
         (node) >= 0; \
         (node) = fdt_next_node((fdt), (node), NULL))

#define device_tree_for_each_compatible(node, fdt, compat) \
    for ((node) = device_tree_find_compatible_node((compat), -1); \
         (node) >= 0; \
         (node) = device_tree_find_compatible_node((compat), (node)))

#endif /* __DEVICE_TREE_H */