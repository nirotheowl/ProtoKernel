#ifndef _IRQ_DOMAIN_H
#define _IRQ_DOMAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <spinlock.h>

struct device_node;
struct irq_desc;
struct irq_chip;
struct radix_tree_root;
struct msi_msg;
struct list_head {
    struct list_head *next, *prev;
};

typedef void (*irq_handler_t)(void *);

// Domain types - determines mapping strategy
enum irq_domain_type {
    DOMAIN_LINEAR,      // Array indexing for dense mappings (most controllers)
    DOMAIN_TREE,        // Radix tree for sparse mappings (PCI MSI)
    DOMAIN_HIERARCHY,   // Delegates to parent with transformation
};

// Forward declaration
struct irq_domain_ops;

// IRQ domain - the central mapping structure
struct irq_domain {
    const char *name;
    enum irq_domain_type type;
    uint32_t domain_id;         // Unique domain identifier
    
    // Domain operations
    const struct irq_domain_ops *ops;
    
    // Mapping data (union based on type)
    union {
        struct {  // LINEAR - array based
            uint32_t size;
            struct irq_desc **linear_map;
        };
        struct {  // TREE - sparse mapping
            struct radix_tree_root *tree;
            uint32_t max_irq;
        };
    };
    
    // Hierarchy support
    struct irq_domain *parent;
    uint32_t parent_irq;
    
    // Associated controller
    struct irq_chip *chip;
    void *chip_data;
    
    // Device tree support
    struct device_node *of_node;
    
    // Reverse mapping cache (hwirq -> virq)
    uint32_t revmap_size;
    uint32_t *revmap;
    
    // Management
    struct list_head link;
    spinlock_t lock;
    uint32_t alloc_base;        // Next virq to allocate
};

// Domain operations
struct irq_domain_ops {
    // Map/unmap hwirq to virq
    int (*map)(struct irq_domain *d, uint32_t virq, uint32_t hwirq);
    void (*unmap)(struct irq_domain *d, uint32_t virq);
    
    // Device tree translation
    int (*xlate)(struct irq_domain *d, const uint32_t *intspec,
                 uint32_t intsize, uint32_t *out_hwirq, uint32_t *out_type);
    
    // MSI allocation support
    int (*alloc)(struct irq_domain *d, uint32_t virq, uint32_t nr_irqs, void *arg);
    void (*free)(struct irq_domain *d, uint32_t virq, uint32_t nr_irqs);
    
    // Hierarchy activation
    int (*activate)(struct irq_domain *d, struct irq_desc *desc, bool early);
    void (*deactivate)(struct irq_domain *d, struct irq_desc *desc);
};

// IRQ chip - hardware controller operations
struct irq_chip {
    const char *name;
    
    // Basic operations
    void (*irq_enable)(struct irq_desc *desc);
    void (*irq_disable)(struct irq_desc *desc);
    void (*irq_mask)(struct irq_desc *desc);
    void (*irq_unmask)(struct irq_desc *desc);
    void (*irq_ack)(struct irq_desc *desc);
    void (*irq_eoi)(struct irq_desc *desc);
    
    // Configuration
    int (*irq_set_type)(struct irq_desc *desc, uint32_t type);
    int (*irq_set_affinity)(struct irq_desc *desc, uint32_t cpu_mask);
    
    // MSI support
    void (*irq_compose_msi_msg)(struct irq_desc *desc, struct msi_msg *msg);
    void (*irq_write_msi_msg)(struct irq_desc *desc, struct msi_msg *msg);
    
    // Cascade support
    void (*irq_calc_mask)(struct irq_desc *desc);
    
    unsigned long flags;
};

// Enhanced IRQ descriptor
struct irq_desc {
    // Identity
    uint32_t irq;               // Virtual IRQ number
    uint32_t hwirq;             // Hardware IRQ number
    
    // Domain linkage
    struct irq_domain *domain;
    struct irq_desc *parent_desc;  // For hierarchy
    
    // Hardware interface
    struct irq_chip *chip;
    void *chip_data;
    
    // Handler chain
    struct irq_action *action;
    
    // State and configuration
    uint32_t status;
    uint32_t trigger_type;
    uint32_t cpu_mask;
    uint32_t depth;             // Nested disable depth
    
    // Statistics
    uint64_t count;
    uint64_t spurious_count;
    uint64_t last_timestamp;
    
    // Synchronization
    spinlock_t lock;
    
    const char *name;
};

// IRQ action - handler registration
struct irq_action {
    irq_handler_t handler;
    void *dev_data;
    uint32_t flags;
    const char *name;
    struct irq_action *next;
};

// ============ Domain Creation ============

// Linear domain - dense hwirq mapping (most common)
struct irq_domain *irq_domain_create_linear(
    struct device_node *node,
    uint32_t size,
    const struct irq_domain_ops *ops,
    void *host_data
);

// Tree domain - sparse hwirq mapping (PCI MSI)
struct irq_domain *irq_domain_create_tree(
    struct device_node *node,
    const struct irq_domain_ops *ops,
    void *host_data
);

// Hierarchy domain - cascaded controllers
struct irq_domain *irq_domain_create_hierarchy(
    struct irq_domain *parent,
    uint32_t size,
    struct device_node *node,
    const struct irq_domain_ops *ops,
    void *host_data
);

// ============ Mapping Management ============

// Create mapping hwirq -> virq
uint32_t irq_create_mapping(struct irq_domain *domain, uint32_t hwirq);

// Find existing mapping
uint32_t irq_find_mapping(struct irq_domain *domain, uint32_t hwirq);

// Remove mapping
void irq_dispose_mapping(uint32_t virq);

// ============ Bulk Allocation (MSI) ============

// Allocate consecutive virqs
int irq_domain_alloc_irqs(struct irq_domain *domain, int nr_irqs,
                         struct device_node *node, void *arg);

// Free allocated virqs
void irq_domain_free_irqs(uint32_t virq, int nr_irqs);

// ============ Device Tree Support ============

// Parse and map interrupt from device tree
uint32_t irq_of_parse_and_map(struct device_node *node, int index);

// ============ Hierarchy Management ============

// Set chip and handler through hierarchy
int irq_domain_set_hwirq_and_chip(struct irq_domain *domain, uint32_t virq,
                                  uint32_t hwirq, struct irq_chip *chip,
                                  void *chip_data);

// Activate through hierarchy
int irq_domain_activate_irq(struct irq_desc *desc, bool early);

// Domain removal
void irq_domain_remove(struct irq_domain *domain);

// Find domain by device tree node
struct irq_domain *irq_find_host(struct device_node *node);

#endif /* _IRQ_DOMAIN_H */