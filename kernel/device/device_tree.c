/*
 * kernel/device/device_tree.c
 * 
 * FDT to device conversion implementation
 * Parses device tree and creates device structures
 */

#include <device/device.h>
#include <device/resource.h>
#include <device/device_tree.h>
#include <drivers/fdt.h>
#include <drivers/fdt_mgr.h>
#include <string.h>
#include <uart.h>
#include <arch_device.h>


// Forward declarations
extern char *device_pool_strdup(const char *str);

// Global FDT blob pointer
static void *fdt_blob = NULL;

// Device type mapping table
typedef struct {
    const char *compatible;
    device_type_t type;
} device_type_map_t;

static const device_type_map_t device_type_map[] = {
    // Generic UART types (not architecture-specific)
    { "ns16550a", DEV_TYPE_UART },
    { "ns16550", DEV_TYPE_UART },
    { "8250", DEV_TYPE_UART },
    
    // PCI
    { "pci-host-ecam-generic", DEV_TYPE_PCI },
    { "pci-host-cam-generic", DEV_TYPE_PCI },
    
    // VirtIO
    { "virtio,mmio", DEV_TYPE_VIRTIO },
    
    // Ethernet
    { "virtio,net", DEV_TYPE_ETHERNET },
    { "smsc,lan91c111", DEV_TYPE_ETHERNET },
    
    // Block devices
    { "virtio,blk", DEV_TYPE_BLOCK },
    
    
    // Default platform device
    { "simple-bus", DEV_TYPE_PLATFORM },
    { "simple-mfd", DEV_TYPE_PLATFORM },
    
    { NULL, DEV_TYPE_UNKNOWN }
};

// Initialize device tree subsystem
int device_tree_init(void *fdt) {
    if (!fdt) {
        // uart_puts("DT: No FDT blob provided\n");
        return -1;
    }
    
    // Validate FDT
    if (fdt_check_header(fdt) != 0) {
        // uart_puts("DT: Invalid FDT header\n");
        return -1;
    }
    
    fdt_blob = fdt;
    
    // uart_puts("DT: Initialized with FDT at ");
    // uart_puthex((uint64_t)fdt);
    // uart_puts("\n");
    
    return 0;
}

// Get device type from compatible string
device_type_t device_tree_compatible_to_type(const char *compatible) {
    const device_type_map_t *map;
    device_type_t arch_type;
    
    if (!compatible) {
        return DEV_TYPE_UNKNOWN;
    }
    
    // First try architecture-specific detection
    arch_type = arch_get_device_type(compatible);
    if (arch_type != DEV_TYPE_UNKNOWN) {
        return arch_type;
    }
    
    // Fall back to generic device type mappings
    for (map = device_type_map; map->compatible; map++) {
        if (strstr(compatible, map->compatible) != NULL) {
            return map->type;
        }
    }
    
    return DEV_TYPE_PLATFORM;  // Default to platform device
}

// Get device type from node
device_type_t device_tree_get_device_type(int node_offset) {
    const char *compatible;
    const char *device_type;
    int len;
    
    if (!fdt_blob) {
        return DEV_TYPE_UNKNOWN;
    }
    
    // First check device_type property
    device_type = fdt_getprop(fdt_blob, node_offset, FDT_PROP_DEVICE_TYPE, &len);
    if (device_type) {
        if (strcmp(device_type, "cpu") == 0) return DEV_TYPE_CPU;
        if (strcmp(device_type, "memory") == 0) return DEV_TYPE_MEMORY;
    }
    
    // Then check compatible string
    compatible = fdt_getprop(fdt_blob, node_offset, FDT_PROP_COMPATIBLE, &len);
    if (compatible) {
        return device_tree_compatible_to_type(compatible);
    }
    
    return DEV_TYPE_PLATFORM;
}

// Check if device is enabled
bool device_tree_is_device_enabled(int node_offset) {
    const char *status;
    int len;
    
    if (!fdt_blob) {
        return false;
    }
    
    // Get status property
    status = fdt_getprop(fdt_blob, node_offset, FDT_PROP_STATUS, &len);
    if (!status) {
        // No status property means enabled
        return true;
    }
    
    // Check status value
    return (strcmp(status, FDT_STATUS_OKAY) == 0 ||
            strcmp(status, "ok") == 0);
}

// Get node name
const char *device_tree_get_node_name(int node_offset) {
    if (!fdt_blob) {
        return NULL;
    }
    
    return fdt_get_name(fdt_blob, node_offset, NULL);
}

// Get compatible string
const char *device_tree_get_compatible(int node_offset) {
    int len;
    
    if (!fdt_blob) {
        return NULL;
    }
    
    return fdt_getprop(fdt_blob, node_offset, FDT_PROP_COMPATIBLE, &len);
}

// Check if node is compatible with string
bool device_tree_is_compatible(int node_offset, const char *compatible) {
    const char *node_compat;
    int len;
    
    if (!fdt_blob || !compatible) {
        return false;
    }
    
    node_compat = fdt_getprop(fdt_blob, node_offset, FDT_PROP_COMPATIBLE, &len);
    if (!node_compat) {
        return false;
    }
    
    // Compatible property can contain multiple strings
    while (len > 0) {
        if (strcmp(node_compat, compatible) == 0) {
            return true;
        }
        int slen = strlen(node_compat) + 1;
        node_compat += slen;
        len -= slen;
    }
    
    return false;
}

// Get number of reg entries
int device_tree_get_reg_count(int node_offset) {
    const uint32_t *reg;
    int len;
    int parent;
    int addr_cells = 2;  // Default for 64-bit
    int size_cells = 2;  // Default for 64-bit
    const uint32_t *prop;
    
    if (!fdt_blob) {
        return 0;
    }
    
    // Get reg property
    reg = fdt_getprop(fdt_blob, node_offset, FDT_PROP_REG, &len);
    if (!reg || len <= 0) {
        return 0;
    }
    
    // Get parent node to find #address-cells and #size-cells
    parent = fdt_parent_offset(fdt_blob, node_offset);
    if (parent >= 0) {
        prop = fdt_getprop(fdt_blob, parent, FDT_PROP_ADDRESS_CELLS, NULL);
        if (prop) {
            addr_cells = fdt32_to_cpu(*prop);
        }
        prop = fdt_getprop(fdt_blob, parent, FDT_PROP_SIZE_CELLS, NULL);
        if (prop) {
            size_cells = fdt32_to_cpu(*prop);
        }
    }
    
    // Calculate number of entries
    int entry_size = (addr_cells + size_cells) * sizeof(uint32_t);
    return len / entry_size;
}

// Get reg entry by index
bool device_tree_get_reg_by_index(int node_offset, int index,
                                 uint64_t *addr, uint64_t *size) {
    const uint32_t *reg;
    int len;
    int parent;
    int addr_cells = 2;
    int size_cells = 2;
    const uint32_t *prop;
    int i;
    
    if (!fdt_blob || !addr || !size) {
        return false;
    }
    
    // Get reg property
    reg = fdt_getprop(fdt_blob, node_offset, FDT_PROP_REG, &len);
    if (!reg || len <= 0) {
        return false;
    }
    
    // Get parent node to find #address-cells and #size-cells
    parent = fdt_parent_offset(fdt_blob, node_offset);
    if (parent >= 0) {
        prop = fdt_getprop(fdt_blob, parent, FDT_PROP_ADDRESS_CELLS, NULL);
        if (prop) {
            addr_cells = fdt32_to_cpu(*prop);
        }
        prop = fdt_getprop(fdt_blob, parent, FDT_PROP_SIZE_CELLS, NULL);
        if (prop) {
            size_cells = fdt32_to_cpu(*prop);
        }
    }
    
    // Calculate entry size and check index
    int entry_size = addr_cells + size_cells;
    int num_entries = len / (entry_size * sizeof(uint32_t));
    if (index >= num_entries) {
        return false;
    }
    
    // Skip to requested entry
    reg += index * entry_size;
    
    // Extract address
    *addr = 0;
    for (i = 0; i < addr_cells; i++) {
        *addr = (*addr << 32) | fdt32_to_cpu(reg[i]);
    }
    
    // Extract size
    *size = 0;
    for (i = 0; i < size_cells; i++) {
        *size = (*size << 32) | fdt32_to_cpu(reg[addr_cells + i]);
    }
    
    return true;
}

// Parse reg property and add memory resources
int device_tree_parse_reg(struct device *dev, int node_offset) {
    uint64_t addr, size;
    int count;
    int i;
    char name[32];
    
    if (!dev || !fdt_blob) {
        return -1;
    }
    
    // Get number of reg entries
    count = device_tree_get_reg_count(node_offset);
    if (count == 0) {
        return 0;
    }
    
    // Add each reg entry as a memory resource
    for (i = 0; i < count && i < DEVICE_MAX_RESOURCES; i++) {
        if (!device_tree_get_reg_by_index(node_offset, i, &addr, &size)) {
            break;
        }
        
        // Create resource name
        if (count == 1) {
            snprintf(name, sizeof(name), "regs");
        } else {
            snprintf(name, sizeof(name), "regs%d", i);
        }
        
        // Add memory resource
        device_add_mem_resource(dev, addr, size, RES_MEM_CACHEABLE, 
                               device_pool_strdup(name));
    }
    
    return i;
}

// Get number of interrupt entries
int device_tree_get_interrupt_count(int node_offset) {
    const uint32_t *interrupts;
    int len;
    
    if (!fdt_blob) {
        return 0;
    }
    
    // Get interrupts property
    interrupts = fdt_getprop(fdt_blob, node_offset, FDT_PROP_INTERRUPTS, &len);
    if (!interrupts || len <= 0) {
        return 0;
    }
    
    // For now, assume 3 cells per interrupt (GIC style)
    return len / (3 * sizeof(uint32_t));
}

// Get interrupt by index
bool device_tree_get_interrupt_by_index(int node_offset, int index,
                                       uint32_t *irq, uint32_t *flags) {
    const uint32_t *interrupts;
    int len;
    int num_entries;
    
    if (!fdt_blob || !irq || !flags) {
        return false;
    }
    
    // Get interrupts property
    interrupts = fdt_getprop(fdt_blob, node_offset, FDT_PROP_INTERRUPTS, &len);
    if (!interrupts || len <= 0) {
        return false;
    }
    
    // For now, assume 3 cells per interrupt (GIC style)
    num_entries = len / (3 * sizeof(uint32_t));
    if (index >= num_entries) {
        return false;
    }
    
    // Skip to requested entry
    interrupts += index * 3;
    
    // Extract interrupt info (GIC format)
    uint32_t int_type = fdt32_to_cpu(interrupts[0]);
    uint32_t int_num = fdt32_to_cpu(interrupts[1]);
    uint32_t int_flags = fdt32_to_cpu(interrupts[2]);
    
    // Convert to linear IRQ number
    if (int_type == 0) {
        // SPI - add 32 to base
        *irq = int_num + 32;
    } else if (int_type == 1) {
        // PPI - add 16 to base
        *irq = int_num + 16;
    } else {
        // Unknown type
        *irq = int_num;
    }
    
    // Convert flags
    *flags = 0;
    if (int_flags & 0x0f) {
        if (int_flags & 0x01) {
            *flags |= RES_IRQ_EDGE | RES_IRQ_RISING;
        }
        if (int_flags & 0x02) {
            *flags |= RES_IRQ_EDGE | RES_IRQ_FALLING;
        }
        if (int_flags & 0x04) {
            *flags |= RES_IRQ_LEVEL | RES_IRQ_HIGHLEVEL;
        }
        if (int_flags & 0x08) {
            *flags |= RES_IRQ_LEVEL | RES_IRQ_LOWLEVEL;
        }
    }
    
    return true;
}

// Parse interrupts property and add IRQ resources
int device_tree_parse_interrupts(struct device *dev, int node_offset) {
    uint32_t irq, flags;
    int count;
    int i;
    char name[32];
    
    if (!dev || !fdt_blob) {
        return -1;
    }
    
    // Get number of interrupt entries
    count = device_tree_get_interrupt_count(node_offset);
    if (count == 0) {
        return 0;
    }
    
    // Add each interrupt as an IRQ resource
    for (i = 0; i < count && dev->num_resources < DEVICE_MAX_RESOURCES; i++) {
        if (!device_tree_get_interrupt_by_index(node_offset, i, &irq, &flags)) {
            break;
        }
        
        // Create resource name
        if (count == 1) {
            snprintf(name, sizeof(name), "irq");
        } else {
            snprintf(name, sizeof(name), "irq%d", i);
        }
        
        // Add IRQ resource
        device_add_irq_resource(dev, irq, flags, device_pool_strdup(name));
    }
    
    return i;
}

// Create a device from FDT node
struct device *device_tree_create_device(int node_offset) {
    struct device *dev;
    const char *name;
    const char *compatible;
    device_type_t type;
    
    if (!fdt_blob) {
        return NULL;
    }
    
    // Check if device is enabled
    if (!device_tree_is_device_enabled(node_offset)) {
        return NULL;
    }
    
    // Get node name
    name = device_tree_get_node_name(node_offset);
    if (!name) {
        return NULL;
    }
    
    // Skip certain nodes
    if (name[0] == '\0') {
        // Root node has empty name, skip it
        return NULL;
    }
    if (strcmp(name, "aliases") == 0 ||
        strcmp(name, "chosen") == 0 ||
        strcmp(name, "cpus") == 0 ||
        strcmp(name, "memory") == 0 ||
        strstr(name, "memory@") != NULL) {
        return NULL;
    }
    
    // Get device type
    type = device_tree_get_device_type(node_offset);
    
    // Register device
    dev = device_register(name, type);
    if (!dev) {
        return NULL;
    }
    
    // Set FDT information
    dev->fdt_offset = node_offset;
    
    // Set compatible string
    compatible = device_tree_get_compatible(node_offset);
    if (compatible) {
        dev->compatible = compatible;  // Points directly to FDT
    }
    
    // Parse resources
    device_tree_parse_reg(dev, node_offset);
    device_tree_parse_interrupts(dev, node_offset);
    
    return dev;
}

// Callback context for device enumeration
typedef struct {
    struct device *parent;
    int count;
} enum_context_t;

// Enumerate devices recursively
static int enumerate_devices_recursive(int node_offset, int depth,
                                     struct device *parent) {
    struct device *dev;
    int child;
    int count = 0;
    const char *name;
    
    // uart_puts("DT: enumerate_devices_recursive called with offset=");
    // uart_puthex(node_offset);
    // uart_puts(", depth=");
    // uart_puthex(depth);
    // uart_puts(", parent=");
    // uart_puthex((uint64_t)parent);
    // uart_puts("\n");
    
    // Get node name for debugging
    // uart_puts("DT: Calling fdt_get_name...\n");
    name = fdt_get_name(fdt_blob, node_offset, NULL);
    if (!name) {
        // uart_puts("DT: Warning - node at offset ");
        // uart_puthex(node_offset);
        // uart_puts(" has no name\n");
        name = "<unnamed>";
    } else {
        // uart_puts("DT: Got name: '");
        // uart_puts(name);
        // uart_puts("'\n");
    }
    
    // Debug output
    // if (depth < 3) {  // Limit debug output to first few levels
    //     int i;
    //     for (i = 0; i < depth; i++) {
    //         uart_puts("  ");
    //     }
    //     uart_puts("Processing node: ");
    //     uart_puts(name);
    //     uart_puts(" at offset ");
    //     uart_puthex(node_offset);
    //     uart_puts("\n");
    // }
    
    // Create device for this node
    // uart_puts("DT: Calling device_tree_create_device...\n");
    dev = device_tree_create_device(node_offset);
    if (dev) {
        // uart_puts("DT: Created device with id=");
        // uart_puthex(dev->id);
        // uart_puts(", name='");
    // uart_puts(dev->name);
    // uart_puts("'\n");
        
        // Set parent relationship
        if (parent) {
            // uart_puts("DT: Adding child to parent...\n");
            device_add_child(parent, dev);
        }
        count++;
        
        // Use this device as parent for children
        parent = dev;
    } else {
        // uart_puts("DT: No device created for this node\n");
    }
    
    // Process children
    // uart_puts("DT: Processing children with fdt_for_each_subnode...\n");
    fdt_for_each_subnode(child, fdt_blob, node_offset) {
        // uart_puts("DT: Found child at offset ");
        // uart_puthex(child);
        // uart_puts("\n");
        count += enumerate_devices_recursive(child, depth + 1, parent);
    }
    
    // uart_puts("DT: enumerate_devices_recursive returning count=");
    // uart_puthex(count);
    // uart_puts("\n");
    
    return count;
}

// Populate all devices from FDT
int device_tree_populate_devices(void) {
    int root_offset;
    int count;
    
    if (!fdt_blob) {
        // uart_puts("DT: No FDT blob available\n");
        return -1;
    }
    
    // uart_puts("DT: FDT blob at ");
    // uart_puthex((uint64_t)fdt_blob);
    // uart_puts("\n");
    
    // Verify FDT is valid
    if (fdt_check_header(fdt_blob) != 0) {
        // uart_puts("DT: Invalid FDT header\n");
        return -1;
    }
    
    // Find root node
    // uart_puts("DT: Finding root node...\n");
    root_offset = fdt_path_offset(fdt_blob, "/");
    if (root_offset < 0) {
        // uart_puts("DT: Failed to find root node (error ");
        // uart_puthex(root_offset);
        // uart_puts(")\n");
        return -1;
    }
    
    // uart_puts("DT: Root node offset: ");
    // uart_puthex(root_offset);
    // uart_puts("\n");
    
    // uart_puts("DT: Enumerating devices from FDT...\n");
    
    // Get root device
    struct device *root_dev = device_get_root();
    // uart_puts("DT: Got root device at ");
    // uart_puthex((uint64_t)root_dev);
    if (root_dev) {
        // uart_puts(", name='");
        // uart_puts(root_dev->name);
        // uart_puts("'");
    }
    // uart_puts("\n");
    
    // Enumerate all devices
    // uart_puts("DT: Calling enumerate_devices_recursive...\n");
    count = enumerate_devices_recursive(root_offset, 0, root_dev);
    
    // uart_puts("DT: Enumerated ");
    // uart_puthex(count);
    // uart_puts(" devices\n");
    
    return count;
}

// Find compatible node
int device_tree_find_compatible_node(const char *compatible, int start_offset) {
    int node;
    
    if (!fdt_blob || !compatible) {
        return -1;
    }
    
    // Start from next node or root
    if (start_offset < 0) {
        node = fdt_next_node(fdt_blob, -1, NULL);
    } else {
        node = fdt_next_node(fdt_blob, start_offset, NULL);
    }
    
    // Search for compatible node
    while (node >= 0) {
        if (device_tree_is_compatible(node, compatible)) {
            return node;
        }
        node = fdt_next_node(fdt_blob, node, NULL);
    }
    
    return -1;
}

// Dump all enumerated devices
void device_tree_dump_devices(void) {
    uart_puts("\nDevice Tree Dump:\n");
    uart_puts("=================\n");
    device_print_tree(NULL, 0);
}