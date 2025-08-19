#include <irq/irq_domain.h>
#include <irq/irq.h>
#include <irq/irq_alloc.h>
#include <memory/kmalloc.h>
#include <memory/slab.h>
#include <lib/radix_tree.h>
#include <string.h>
#include <panic.h>
#include <uart.h>

// Special marker for reserved but unmapped hwirq slots
#define HWIRQ_RESERVED_MARKER ((void *)0x1)

// Global domain list management
static struct list_head irq_domain_list = {
    .next = &irq_domain_list,
    .prev = &irq_domain_list
};
static spinlock_t irq_domain_list_lock = SPINLOCK_INITIALIZER;
static uint32_t next_domain_id = 1;

// Default domain for quick lookups
static struct irq_domain *irq_default_domain = NULL;

// List manipulation helpers
static inline void list_init(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

static inline void list_add(struct list_head *new, struct list_head *head) {
    new->next = head->next;
    new->prev = head;
    head->next->prev = new;
    head->next = new;
}

static inline void list_del(struct list_head *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    entry->next = entry;
    entry->prev = entry;
}

static inline bool list_empty(struct list_head *head) {
    return head->next == head;
}

// Helper to allocate and initialize base domain structure
static struct irq_domain *irq_domain_alloc(struct device_node *node,
                                           const struct irq_domain_ops *ops,
                                           void *host_data) {
    struct irq_domain *domain;
    
    domain = kmalloc(sizeof(struct irq_domain), KM_ZERO);
    if (!domain) {
        return NULL;
    }
 
    // Initialize common fields
    domain->ops = ops;
    domain->chip_data = host_data;
    domain->of_node = node;
    spin_lock_init(&domain->lock);
    list_init(&domain->link);
    
    // Assign unique domain ID
    unsigned long flags;
    spin_lock_irqsave(&irq_domain_list_lock, flags);
    domain->domain_id = next_domain_id++;
    spin_unlock_irqrestore(&irq_domain_list_lock, flags);
    
    return domain;
}

// Register domain in global list
static void irq_domain_register(struct irq_domain *domain) {
    unsigned long flags;
    
    spin_lock_irqsave(&irq_domain_list_lock, flags);
    list_add(&domain->link, &irq_domain_list);
    
    // Set as default if first domain
    if (!irq_default_domain) {
        irq_default_domain = domain;
    }
    
    spin_unlock_irqrestore(&irq_domain_list_lock, flags);
}

// Create a linear domain for dense hwirq mappings
struct irq_domain *irq_domain_create_linear(struct device_node *node,
                                           uint32_t size,
                                           const struct irq_domain_ops *ops,
                                           void *host_data) {
    struct irq_domain *domain;
    
    if (size == 0 || size > 10000) {  // Sanity check
        return NULL;
    }
    
    domain = irq_domain_alloc(node, ops, host_data);
    if (!domain) {
        return NULL;
    }
    
    domain->type = DOMAIN_LINEAR;
    domain->size = size;
    domain->name = "linear";
    
    // Allocate linear mapping array
    domain->linear_map = kmalloc(size * sizeof(struct irq_desc *), KM_ZERO);
    if (!domain->linear_map) {
        kfree(domain);
        return NULL;
    }
    
    // Already zeroed by KM_ZERO
    
    // Allocate reverse mapping cache
    domain->revmap_size = size;
    domain->revmap = kmalloc(size * sizeof(uint32_t), 0);
    if (!domain->revmap) {
        kfree(domain->linear_map);
        kfree(domain);
        return NULL;
    }
    
    memset(domain->revmap, 0xFF, size * sizeof(uint32_t));  // Initialize to invalid
    
    // Register domain
    irq_domain_register(domain);
    
    return domain;
}

// Create a tree domain for sparse hwirq mappings
struct irq_domain *irq_domain_create_tree(struct device_node *node,
                                         const struct irq_domain_ops *ops,
                                         void *host_data) {
    struct irq_domain *domain;
    
    domain = irq_domain_alloc(node, ops, host_data);
    if (!domain) {
        return NULL;
    }
    
    domain->type = DOMAIN_TREE;
    domain->name = "tree";
    
    // Allocate radix tree for sparse mapping
    domain->tree = kmalloc(sizeof(struct radix_tree_root), KM_ZERO);
    if (!domain->tree) {
        kfree(domain);
        return NULL;
    }
    
    // Initialize radix tree
    radix_tree_init(domain->tree);
    
    // Set max_irq to a reasonable limit for sparse mappings
    domain->max_irq = 0xFFFFFF; // 16M should be enough
    
    // Register the domain
    irq_domain_register(domain);
    
    return domain;
}

// Create a hierarchy domain for cascaded controllers
struct irq_domain *irq_domain_create_hierarchy(struct irq_domain *parent,
                                              uint32_t size,
                                              struct device_node *node,
                                              const struct irq_domain_ops *ops,
                                              void *host_data) {
    struct irq_domain *domain;
    
    if (!parent) {
        return NULL;  // Hierarchy domain must have a parent
    }
    
    domain = irq_domain_alloc(node, ops, host_data);
    if (!domain) {
        return NULL;
    }
    
    domain->type = DOMAIN_HIERARCHY;
    domain->parent = parent;
    domain->size = size;
    domain->name = "hierarchy";
    
    // Allocate linear mapping array for child domain
    domain->linear_map = kmalloc(size * sizeof(struct irq_desc *), KM_ZERO);
    if (!domain->linear_map) {
        kfree(domain);
        return NULL;
    }
    
    // Allocate reverse mapping cache
    domain->revmap_size = size;
    domain->revmap = kmalloc(size * sizeof(uint32_t), 0);
    if (!domain->revmap) {
        kfree(domain->linear_map);
        kfree(domain);
        return NULL;
    }
    
    memset(domain->revmap, 0xFF, size * sizeof(uint32_t));  // Initialize to invalid
    
    // Register domain
    irq_domain_register(domain);
    
    return domain;
}

// Create a mapping from hardware IRQ to virtual IRQ
uint32_t irq_create_mapping(struct irq_domain *domain, uint32_t hwirq) {
    uint32_t virq;
    struct irq_desc *desc;
    unsigned long flags;
    int ret;
    
    if (!domain) {
        domain = irq_default_domain;
        if (!domain) {
            return IRQ_INVALID;
        }
    }
    
    // Check if mapping already exists
    virq = irq_find_mapping(domain, hwirq);
    if (virq != IRQ_INVALID) {
        return virq;
    }
    
    // Allocate a virtual IRQ number
    virq = virq_alloc();
    if (virq == IRQ_INVALID) {
        return IRQ_INVALID;
    }
    
    // Allocate descriptor for this virq
    desc = irq_desc_alloc(virq);
    if (!desc) {
        virq_free(virq);
        return IRQ_INVALID;
    }
    
    spin_lock_irqsave(&domain->lock, flags);
    
    // Store mapping based on domain type
    if (domain->type == DOMAIN_LINEAR || domain->type == DOMAIN_HIERARCHY) {
        if (hwirq >= domain->size) {
            spin_unlock_irqrestore(&domain->lock, flags);
            virq_free(virq);
            irq_desc_free(desc);
            return IRQ_INVALID;
        }
        
        // Store in linear map
        domain->linear_map[hwirq] = desc;
        
        // Update reverse map
        if (hwirq < domain->revmap_size) {
            domain->revmap[hwirq] = virq;
        }
    } else if (domain->type == DOMAIN_TREE) {
        // Check if slot is reserved or already occupied
        void *existing = radix_tree_lookup(domain->tree, hwirq);
        if (existing == HWIRQ_RESERVED_MARKER) {
            // Replace reserved marker with actual descriptor
            radix_tree_delete(domain->tree, hwirq);
            ret = radix_tree_insert(domain->tree, hwirq, desc);
        } else if (existing != NULL) {
            // Already occupied by real descriptor - shouldn't happen due to earlier check
            spin_unlock_irqrestore(&domain->lock, flags);
            virq_free(virq);
            irq_desc_free(desc);
            return IRQ_INVALID;
        } else {
            // Empty slot, insert normally
            ret = radix_tree_insert(domain->tree, hwirq, desc);
        }
        
        if (ret < 0) {
            spin_unlock_irqrestore(&domain->lock, flags);
            virq_free(virq);
            irq_desc_free(desc);
            return IRQ_INVALID;
        }
    }
    
    // For hierarchical domains, allocate parent IRQ
    if (domain->type == DOMAIN_HIERARCHY && domain->parent) {
            uint32_t parent_virq;
            uint32_t parent_hwirq;
            
            // Transform child hwirq to parent hwirq
            if (domain->ops && domain->ops->child_to_parent_hwirq) {
                parent_hwirq = domain->ops->child_to_parent_hwirq(domain, hwirq);
            } else {
                // Default: identity mapping
                parent_hwirq = hwirq;
            }
            
            // We need to release the lock before recursive call to avoid deadlock
            spin_unlock_irqrestore(&domain->lock, flags);
            
            
            // Create parent mapping
            parent_virq = irq_create_mapping(domain->parent, parent_hwirq);
            
            // Re-acquire lock to finish setup
            spin_lock_irqsave(&domain->lock, flags);
            
            if (parent_virq == IRQ_INVALID) {
                // Clean up child mapping
                domain->linear_map[hwirq] = NULL;
                if (hwirq < domain->revmap_size) {
                    domain->revmap[hwirq] = IRQ_INVALID;
                }
                spin_unlock_irqrestore(&domain->lock, flags);
                virq_free(virq);
                irq_desc_free(desc);
                return IRQ_INVALID;
            }
            
            // Link child descriptor to parent
            struct irq_desc *parent_desc = irq_to_desc(parent_virq);
            if (parent_desc) {
                desc->parent_desc = parent_desc;
                domain->parent_irq = parent_virq;
            }
    }
    
    // Update descriptor
    desc->hwirq = hwirq;
    desc->domain = domain;
    desc->chip = domain->chip;
    desc->chip_data = domain->chip_data;
    
    spin_unlock_irqrestore(&domain->lock, flags);
    
    // Call domain map operation if provided
    if (domain->ops && domain->ops->map) {
        ret = domain->ops->map(domain, virq, hwirq);
        if (ret < 0) {
            irq_dispose_mapping(virq);
            return IRQ_INVALID;
        }
    }
    
    return virq;
}

// Find existing mapping from hardware IRQ to virtual IRQ
uint32_t irq_find_mapping(struct irq_domain *domain, uint32_t hwirq) {
    uint32_t virq = IRQ_INVALID;
    unsigned long flags;
    
    if (!domain) {
        domain = irq_default_domain;
        if (!domain) {
            return IRQ_INVALID;
        }
    }
    
    spin_lock_irqsave(&domain->lock, flags);
    
    if (domain->type == DOMAIN_LINEAR || domain->type == DOMAIN_HIERARCHY) {
        if (hwirq < domain->size && domain->linear_map[hwirq]) {
            virq = domain->linear_map[hwirq]->irq;
        } else if (hwirq < domain->revmap_size) {
            virq = domain->revmap[hwirq];
        }
    } else if (domain->type == DOMAIN_TREE) {
        // Look up in radix tree
        struct irq_desc *desc = radix_tree_lookup(domain->tree, hwirq);
        if (desc && desc != HWIRQ_RESERVED_MARKER) {
            virq = desc->irq;
        }
    }
    
    spin_unlock_irqrestore(&domain->lock, flags);
    
    return virq;
}

// Remove a mapping
void irq_dispose_mapping(uint32_t virq) {
    struct irq_desc *desc;
    struct irq_domain *domain;
    uint32_t hwirq;
    unsigned long flags;
    
    if (virq == IRQ_INVALID) {
        return;
    }
    
    desc = irq_to_desc(virq);
    if (!desc) {
        return;
    }
    
    domain = desc->domain;
    hwirq = desc->hwirq;
    
    if (!domain) {
        return;
    }
    
    // For hierarchical domains, clean up parent mapping first
    if (domain->type == DOMAIN_HIERARCHY && desc->parent_desc) {
        uint32_t parent_virq = desc->parent_desc->irq;
        desc->parent_desc = NULL;
        irq_dispose_mapping(parent_virq);
    }
    
    // Call domain unmap operation if provided
    if (domain->ops && domain->ops->unmap) {
        domain->ops->unmap(domain, virq);
    }
    
    spin_lock_irqsave(&domain->lock, flags);
    
    // Remove from domain mapping
    if (domain->type == DOMAIN_LINEAR || domain->type == DOMAIN_HIERARCHY) {
        if (hwirq < domain->size && domain->linear_map[hwirq] == desc) {
            domain->linear_map[hwirq] = NULL;
        }
        if (hwirq < domain->revmap_size) {
            domain->revmap[hwirq] = IRQ_INVALID;
        }
    } else if (domain->type == DOMAIN_TREE) {
        // Remove from radix tree
        radix_tree_delete(domain->tree, hwirq);
    }
    
    spin_unlock_irqrestore(&domain->lock, flags);
    
    // Clear descriptor domain info
    desc->domain = NULL;
    desc->hwirq = IRQ_INVALID;
    desc->parent_desc = NULL;
    
    // Free the virtual IRQ
    virq_free(virq);
    
    // Free descriptor if no handlers attached
    if (!desc->action) {
        irq_desc_free(desc);
    }
}

// Remove a domain
void irq_domain_remove(struct irq_domain *domain) {
    unsigned long flags;
    uint32_t i;
    
    if (!domain) {
        return;
    }
    
    // Remove all mappings
    if (domain->type == DOMAIN_LINEAR || domain->type == DOMAIN_HIERARCHY) {
        for (i = 0; i < domain->size; i++) {
            if (domain->linear_map[i]) {
                irq_dispose_mapping(domain->linear_map[i]->irq);
            }
        }
    } else if (domain->type == DOMAIN_TREE) {
        // For tree domains, we need to iterate through all mappings
        // This is less efficient but necessary for sparse mappings
        struct radix_tree_iter iter = {0};
        void *slot;
        
        while ((slot = radix_tree_next_slot(domain->tree, &iter, 0)) != NULL) {
            struct irq_desc *desc = slot;
            if (desc) {
                irq_dispose_mapping(desc->irq);
            }
        }
    }
    
    // Remove from global list
    spin_lock_irqsave(&irq_domain_list_lock, flags);
    list_del(&domain->link);
    
    // Clear default if this was it
    if (irq_default_domain == domain) {
        irq_default_domain = NULL;
        // Try to find another domain as default
        if (!list_empty(&irq_domain_list)) {
            struct list_head *first = irq_domain_list.next;
            irq_default_domain = (struct irq_domain *)((char *)first - 
                                offsetof(struct irq_domain, link));
        }
    }
    
    spin_unlock_irqrestore(&irq_domain_list_lock, flags);
    
    // Free domain resources
    if (domain->type == DOMAIN_LINEAR || domain->type == DOMAIN_HIERARCHY) {
        if (domain->linear_map) {
            kfree(domain->linear_map);
        }
        if (domain->revmap) {
            kfree(domain->revmap);
        }
    } else if (domain->type == DOMAIN_TREE) {
        if (domain->tree) {
            kfree(domain->tree);
        }
    }
    
    kfree(domain);
}

// Find domain by device tree node
struct irq_domain *irq_find_host(struct device_node *node) {
    struct irq_domain *domain = NULL;
    struct list_head *pos;
    unsigned long flags;
    
    spin_lock_irqsave(&irq_domain_list_lock, flags);
    
    for (pos = irq_domain_list.next; pos != &irq_domain_list; pos = pos->next) {
        domain = (struct irq_domain *)((char *)pos - offsetof(struct irq_domain, link));
        if (domain->of_node == node) {
            spin_unlock_irqrestore(&irq_domain_list_lock, flags);
            return domain;
        }
    }
    
    spin_unlock_irqrestore(&irq_domain_list_lock, flags);
    
    return NULL;
}

// Set chip and handler through hierarchy
int irq_domain_set_hwirq_and_chip(struct irq_domain *domain, uint32_t virq,
                                 uint32_t hwirq, struct irq_chip *chip,
                                 void *chip_data) {
    struct irq_desc *desc;
    
    desc = irq_to_desc(virq);
    if (!desc) {
        return -1;
    }
    
    desc->hwirq = hwirq;
    desc->chip = chip;
    desc->chip_data = chip_data;
    
    return 0;
}

// Activate through hierarchy
int irq_domain_activate_irq(struct irq_desc *desc, bool early) {
    struct irq_domain *domain;
    int ret = 0;
    
    if (!desc) {
        return -1;
    }
    
    domain = desc->domain;
    if (!domain) {
        return 0;  // No domain, nothing to activate
    }
    
    // For hierarchical domains, activate parent first
    if (domain->type == DOMAIN_HIERARCHY && desc->parent_desc) {
        ret = irq_domain_activate_irq(desc->parent_desc, early);
        if (ret < 0) {
            return ret;
        }
    }
    
    // Then activate this level
    if (domain->ops && domain->ops->activate) {
        ret = domain->ops->activate(domain, desc, early);
        if (ret < 0) {
            // Deactivate parent on failure
            if (domain->type == DOMAIN_HIERARCHY && desc->parent_desc &&
                desc->parent_desc->domain && desc->parent_desc->domain->ops &&
                desc->parent_desc->domain->ops->deactivate) {
                desc->parent_desc->domain->ops->deactivate(desc->parent_desc->domain, 
                                                           desc->parent_desc);
            }
            return ret;
        }
    }
    
    return 0;
}

// Deactivate through hierarchy
void irq_domain_deactivate_irq(struct irq_desc *desc) {
    struct irq_domain *domain;
    
    if (!desc) {
        return;
    }
    
    domain = desc->domain;
    if (!domain) {
        return;
    }
    
    // Deactivate this level first
    if (domain->ops && domain->ops->deactivate) {
        domain->ops->deactivate(domain, desc);
    }
    
    // Then deactivate parent
    if (domain->type == DOMAIN_HIERARCHY && desc->parent_desc) {
        irq_domain_deactivate_irq(desc->parent_desc);
    }
}

// Allocate consecutive virqs for MSI support
int irq_domain_alloc_irqs(struct irq_domain *domain, int nr_irqs,
                         struct device_node *node, void *arg) {
    uint32_t virq_base;
    int i;
    
    if (!domain || nr_irqs <= 0) {
        return -1;
    }
    
    // Allocate consecutive virtual IRQs
    virq_base = virq_alloc_range(nr_irqs);
    if (virq_base == IRQ_INVALID) {
        return -1;
    }
    
    // Call domain alloc operation if provided
    if (domain->ops && domain->ops->alloc) {
        int ret = domain->ops->alloc(domain, virq_base, nr_irqs, arg);
        if (ret < 0) {
            virq_free_range(virq_base, nr_irqs);
            return ret;
        }
    }
    
    // Allocate descriptors for each virq
    for (i = 0; i < nr_irqs; i++) {
        struct irq_desc *desc = irq_desc_alloc(virq_base + i);
        if (!desc) {
            // Clean up on failure
            for (i--; i >= 0; i--) {
                irq_desc_free(irq_to_desc(virq_base + i));
            }
            virq_free_range(virq_base, nr_irqs);
            return -1;
        }
        desc->domain = domain;
        desc->chip = domain->chip;
        desc->chip_data = domain->chip_data;
    }
    
    return virq_base;
}

// Free allocated virqs
void irq_domain_free_irqs(uint32_t virq, int nr_irqs) {
    int i;
    struct irq_desc *desc;
    struct irq_domain *domain = NULL;
    
    if (virq == IRQ_INVALID || nr_irqs <= 0) {
        return;
    }
    
    // Get domain from first descriptor
    desc = irq_to_desc(virq);
    if (desc) {
        domain = desc->domain;
    }
    
    // Call domain free operation if provided
    if (domain && domain->ops && domain->ops->free) {
        domain->ops->free(domain, virq, nr_irqs);
    }
    
    // Free descriptors and virqs
    for (i = 0; i < nr_irqs; i++) {
        irq_dispose_mapping(virq + i);
    }
}

// Set the default IRQ domain
void irq_set_default_domain(struct irq_domain *domain) {
    unsigned long flags;
    
    spin_lock_irqsave(&irq_domain_list_lock, flags);
    irq_default_domain = domain;
    spin_unlock_irqrestore(&irq_domain_list_lock, flags);
}

// ============ Range Allocation Functions for MSI Support ============

// Find a free hwirq range in a tree domain
static int tree_domain_find_free_range(struct irq_domain *domain, 
                                       uint32_t count, uint32_t *hwirq_base) {
    struct radix_tree_iter iter = {0};
    uint32_t start = 0;
    uint32_t consecutive = 0;
    uint32_t current = 0;
    void *slot;
    unsigned long flags;
    
    if (!domain || domain->type != DOMAIN_TREE || !hwirq_base || count == 0) {
        return -1;
    }
    
    spin_lock_irqsave(&domain->lock, flags);
    
    // Simple algorithm: find first gap of 'count' consecutive hwirqs
    // Start from 0 and look for gaps in the radix tree
    while (current < domain->max_irq) {
        slot = radix_tree_lookup(domain->tree, current);
        if (!slot) {
            // Found a free slot
            if (consecutive == 0) {
                start = current;
            }
            consecutive++;
            
            if (consecutive >= count) {
                // Found enough consecutive free slots
                *hwirq_base = start;
                spin_unlock_irqrestore(&domain->lock, flags);
                return 0;
            }
        } else {
            // Slot is occupied, reset counter
            consecutive = 0;
        }
        current++;
    }
    
    spin_unlock_irqrestore(&domain->lock, flags);
    return -1; // No suitable range found
}

// Reserve a hwirq range in a tree domain (mark as allocated)
static int tree_domain_reserve_range(struct irq_domain *domain,
                                     uint32_t hwirq_base, uint32_t count) {
    uint32_t i;
    int ret;
    unsigned long flags;
    
    if (!domain || domain->type != DOMAIN_TREE || count == 0) {
        return -1;
    }
    
    spin_lock_irqsave(&domain->lock, flags);
    
    // First check if any slot in the range is already occupied
    for (i = 0; i < count; i++) {
        void *existing = radix_tree_lookup(domain->tree, hwirq_base + i);
        if (existing != NULL) {
            spin_unlock_irqrestore(&domain->lock, flags);
            return -1; // Range not available
        }
    }
    
    // Insert reserved markers to mark range as allocated
    for (i = 0; i < count; i++) {
        ret = radix_tree_insert(domain->tree, hwirq_base + i, HWIRQ_RESERVED_MARKER);
        if (ret < 0) {
            // Rollback on failure
            while (i > 0) {
                i--;
                radix_tree_delete(domain->tree, hwirq_base + i);
            }
            spin_unlock_irqrestore(&domain->lock, flags);
            return -1;
        }
    }
    
    spin_unlock_irqrestore(&domain->lock, flags);
    return 0;
}

// Release a reserved hwirq range in a tree domain
static void tree_domain_release_range(struct irq_domain *domain,
                                      uint32_t hwirq_base, uint32_t count) {
    uint32_t i;
    unsigned long flags;
    
    if (!domain || domain->type != DOMAIN_TREE || count == 0) {
        return;
    }
    
    spin_lock_irqsave(&domain->lock, flags);
    
    // Remove reserved markers (but preserve actual IRQ descriptors)
    for (i = 0; i < count; i++) {
        void *entry = radix_tree_lookup(domain->tree, hwirq_base + i);
        if (entry == HWIRQ_RESERVED_MARKER) {
            radix_tree_delete(domain->tree, hwirq_base + i);
        }
        // If it's a real IRQ descriptor, leave it alone
    }
    
    spin_unlock_irqrestore(&domain->lock, flags);
}

// Public API: Allocate a consecutive hwirq range for MSI
int irq_domain_alloc_hwirq_range(struct irq_domain *domain, uint32_t count,
                                 uint32_t *hwirq_base) {
    if (!domain || !hwirq_base || count == 0) {
        return -1;
    }
    
    if (domain->type == DOMAIN_TREE) {
        // Find a free range
        int result = tree_domain_find_free_range(domain, count, hwirq_base);
        if (result == 0) {
            // Reserve the range to mark it as allocated
            result = tree_domain_reserve_range(domain, *hwirq_base, count);
            if (result < 0) {
                // Failed to reserve, range is not allocated
                return -1;
            }
        }
        return result;
    }
    
    // For linear domains, find consecutive free slots
    if (domain->type == DOMAIN_LINEAR) {
        uint32_t i, j;
        uint32_t consecutive = 0;
        uint32_t start = 0;
        unsigned long flags;
        
        spin_lock_irqsave(&domain->lock, flags);
        
        for (i = 0; i < domain->size; i++) {
            if (!domain->linear_map[i]) {
                if (consecutive == 0) {
                    start = i;
                }
                consecutive++;
                
                if (consecutive >= count) {
                    *hwirq_base = start;
                    spin_unlock_irqrestore(&domain->lock, flags);
                    return 0;
                }
            } else {
                consecutive = 0;
            }
        }
        
        spin_unlock_irqrestore(&domain->lock, flags);
    }
    
    return -1;
}

// Public API: Free a hwirq range
void irq_domain_free_hwirq_range(struct irq_domain *domain,
                                uint32_t hwirq_base, uint32_t count) {
    if (!domain || count == 0) {
        return;
    }
    
    if (domain->type == DOMAIN_TREE) {
        tree_domain_release_range(domain, hwirq_base, count);
    }
    // For linear domains, disposal is handled by irq_dispose_mapping
}
