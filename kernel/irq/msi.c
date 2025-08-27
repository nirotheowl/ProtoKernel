#include <irq/msi.h>
#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <memory/kmalloc.h>
#include <memory/slab.h>
#include <string.h>
#include <panic.h>

// Helper to allocate a new MSI descriptor
static struct msi_desc *msi_desc_alloc_internal(void) {
    struct msi_desc *desc;
    
    desc = kmalloc(sizeof(*desc), KM_ZERO);
    if (!desc) {
        return NULL;
    }
    
    // Initialize list head
    desc->list.next = &desc->list;
    desc->list.prev = &desc->list;
    desc->refcount = 1;
    
    return desc;
}

// Allocate MSI descriptor for a device
struct msi_desc *msi_desc_alloc(struct device *dev, uint32_t nvec) {
    struct msi_desc *desc;
    
    if (!dev || nvec == 0 || nvec > MSI_MAX_VECTORS) {
        return NULL;
    }
    
    desc = msi_desc_alloc_internal();
    if (!desc) {
        return NULL;
    }
    
    desc->dev = dev;
    desc->multiple = 0;
    
    // Calculate log2 of nvec
    while ((1U << desc->multiple) < nvec) {
        desc->multiple++;
    }
    
    return desc;
}

// Free MSI descriptor
void msi_desc_free(struct msi_desc *desc) {
    if (!desc) {
        return;
    }
    
    desc->refcount--;
    if (desc->refcount > 0) {
        return;
    }
    
    // Remove from list if still linked
    if (desc->list.next != &desc->list) {
        desc->list.next->prev = desc->list.prev;
        desc->list.prev->next = desc->list.next;
    }
    
    kfree(desc);
}

// Add descriptor to device's MSI list
int msi_desc_list_add(struct msi_device_data *msi_data, struct msi_desc *desc) {
    unsigned long flags;
    int ret;
    
    if (!msi_data || !desc) {
        return -1;
    }
    
    spin_lock_irqsave(&msi_data->lock, flags);
    ret = msi_desc_list_add_locked(msi_data, desc);
    spin_unlock_irqrestore(&msi_data->lock, flags);
    
    return ret;
}

// Add descriptor to device's MSI list (caller holds lock)
int msi_desc_list_add_locked(struct msi_device_data *msi_data, struct msi_desc *desc) {
    if (!msi_data || !desc) {
        return -1;
    }
    
    if (msi_data->num_vectors >= msi_data->max_vectors) {
        return -1;
    }
    
    // Add to tail of list
    desc->list.next = &msi_data->list;
    desc->list.prev = msi_data->list.prev;
    msi_data->list.prev->next = &desc->list;
    msi_data->list.prev = &desc->list;
    
    msi_data->num_vectors++;
    desc->refcount++;
    
    return 0;
}

// Initialize MSI support for a device
int msi_device_init(struct device *dev) {
    struct msi_device_data *msi_data;
    size_t bitmap_size;
    
    if (!dev) {
        return -1;
    }
    
    msi_data = kmalloc(sizeof(*msi_data), KM_ZERO);
    if (!msi_data) {
        return -1;
    }
    
    // Initialize list head
    msi_data->list.next = &msi_data->list;
    msi_data->list.prev = &msi_data->list;
    
    spin_lock_init(&msi_data->lock);
    msi_data->max_vectors = MSI_MAX_VECTORS;
    
    // Allocate bitmap for vector tracking
    bitmap_size = (msi_data->max_vectors + 7) / 8;
    msi_data->used_vectors = kmalloc(bitmap_size, KM_ZERO);
    if (!msi_data->used_vectors) {
        kfree(msi_data);
        return -1;
    }
    
    dev->msi_data = msi_data;
    
    return 0;
}

// Cleanup MSI support for a device
void msi_device_cleanup(struct device *dev) {
    struct msi_device_data *msi_data;
    struct msi_desc *desc, *next;
    unsigned long flags;
    
    if (!dev || !dev->msi_data) {
        return;
    }
    
    msi_data = dev->msi_data;
    
    spin_lock_irqsave(&msi_data->lock, flags);
    
    // Walk list and free descriptors
    desc = (struct msi_desc *)msi_data->list.next;
    while (&desc->list != &msi_data->list) {
        next = (struct msi_desc *)desc->list.next;
        
        // Remove from list
        desc->list.next->prev = desc->list.prev;
        desc->list.prev->next = desc->list.next;
        
        desc->refcount--;
        if (desc->refcount == 0) {
            kfree(desc);
        }
        
        desc = next;
    }
    
    spin_unlock_irqrestore(&msi_data->lock, flags);
    
    kfree(msi_data->used_vectors);
    kfree(msi_data);
    dev->msi_data = NULL;
}

// Helper to allocate vector indices within device
// NOTE: This is for device-local tracking only. The actual hwirq allocation
// will use tree domains via irq_domain_alloc_hwirq_range()
static int msi_allocate_vector_indices(struct device *dev, uint32_t nvec) {
    struct msi_device_data *msi_data = dev->msi_data;
    uint32_t i, start = 0;
    uint32_t consecutive = 0;
    
    // Find consecutive free vectors in device's local tracking
    for (i = 0; i < msi_data->max_vectors; i++) {
        if (!(msi_data->used_vectors[i / 8] & (1 << (i % 8)))) {
            if (consecutive == 0) {
                start = i;
            }
            consecutive++;
            
            if (consecutive == nvec) {
                // Mark vectors as used
                for (uint32_t j = start; j < start + nvec; j++) {
                    msi_data->used_vectors[j / 8] |= (1 << (j % 8));
                }
                return start;
            }
        } else {
            consecutive = 0;
        }
    }
    
    return -1;
}

// Helper to free vector indices within device
static void msi_free_vector_indices(struct device *dev, uint32_t start, uint32_t nvec) {
    struct msi_device_data *msi_data = dev->msi_data;
    uint32_t i;
    
    for (i = start; i < start + nvec; i++) {
        msi_data->used_vectors[i / 8] &= ~(1 << (i % 8));
    }
}

// Allocate MSI vectors for a device
// This function will need to be updated to:
// 1. Get the MSI domain (tree domain) from the device or parent
// 2. Use irq_domain_alloc_hwirq_range() to allocate consecutive hwirqs
// 3. Create virq mappings using irq_create_mapping()
int msi_alloc_vectors(struct device *dev, uint32_t min_vecs,
                     uint32_t max_vecs, unsigned int flags) {
    struct msi_device_data *msi_data;
    struct msi_desc *desc;
    uint32_t nvec, allocated_base;
    uint32_t i, j;
    int ret;
    unsigned long irqflags;
    
    if (!dev || !dev->msi_data || min_vecs == 0 || min_vecs > max_vecs ||
        max_vecs > MSI_MAX_VECTORS) {
        return -1;
    }
    
    msi_data = dev->msi_data;
    
    // Determine number of vectors to allocate
    nvec = max_vecs;
    if (flags & MSI_FLAG_USE_DEF_NUM_VECS) {
        nvec = min_vecs;
    }
    
    if (!(flags & MSI_FLAG_MULTI_VECTOR)) {
        nvec = 1;
    }
    
    spin_lock_irqsave(&msi_data->lock, irqflags);
    
    // Allocate vector indices for device-local tracking
    allocated_base = msi_allocate_vector_indices(dev, nvec);
    if (allocated_base < 0) {
        spin_unlock_irqrestore(&msi_data->lock, irqflags);
        return -1;
    }
    
    // TODO: When MSI domain is available:
    // 1. Get MSI domain from device or parent
    //    struct irq_domain *msi_domain = dev->msi_domain;
    // 2. Allocate hwirq range from tree domain
    //    uint32_t hwirq_base;
    //    if (irq_domain_alloc_hwirq_range(msi_domain, nvec, &hwirq_base) < 0) {
    //        // Handle allocation failure
    //    }
    // 3. Create virq mappings
    //    for (i = 0; i < nvec; i++) {
    //        desc->irq = irq_create_mapping(msi_domain, hwirq_base + i);
    //    }
    
    // Create descriptors for each vector
    for (i = 0; i < nvec; i++) {
        desc = msi_desc_alloc_internal();
        if (!desc) {
            // Cleanup on failure
            for (j = 0; j < i; j++) {
                struct msi_desc *d = (struct msi_desc *)msi_data->list.next;
                while (&d->list != &msi_data->list) {
                    if (d->hwirq == allocated_base + j) {
                        d->list.next->prev = d->list.prev;
                        d->list.prev->next = d->list.next;
                        kfree(d);
                        break;
                    }
                    d = (struct msi_desc *)d->list.next;
                }
            }
            
            msi_free_vector_indices(dev, allocated_base, nvec);
            spin_unlock_irqrestore(&msi_data->lock, irqflags);
            return -1;
        }
        
        desc->dev = dev;
        desc->hwirq = allocated_base + i;
        desc->msi_attrib = flags & 0xFFFF;
        
        ret = msi_desc_list_add_locked(msi_data, desc);
        if (ret < 0) {
            kfree(desc);
            
            // Cleanup on failure
            for (j = 0; j < i; j++) {
                struct msi_desc *d = (struct msi_desc *)msi_data->list.next;
                while (&d->list != &msi_data->list) {
                    if (d->hwirq == allocated_base + j) {
                        d->list.next->prev = d->list.prev;
                        d->list.prev->next = d->list.next;
                        kfree(d);
                        break;
                    }
                    d = (struct msi_desc *)d->list.next;
                }
            }
            
            msi_free_vector_indices(dev, allocated_base, nvec);
            spin_unlock_irqrestore(&msi_data->lock, irqflags);
            return -1;
        }
    }
    
    spin_unlock_irqrestore(&msi_data->lock, irqflags);
    
    return nvec;
}

// Free all MSI vectors for a device
void msi_free_vectors(struct device *dev) {
    struct msi_device_data *msi_data;
    struct msi_desc *desc, *next;
    unsigned long flags;
    
    if (!dev || !dev->msi_data) {
        return;
    }
    
    msi_data = dev->msi_data;
    
    spin_lock_irqsave(&msi_data->lock, flags);
    
    // TODO: When MSI domain is available:
    // Track hwirq ranges to free from tree domain via irq_domain_free_hwirq_range()
    
    // Walk list and free descriptors
    desc = (struct msi_desc *)msi_data->list.next;
    while (&desc->list != &msi_data->list) {
        next = (struct msi_desc *)desc->list.next;
        
        // Dispose of IRQ mapping if exists
        if (desc->irq) {
            irq_dispose_mapping(desc->irq);
            desc->irq = 0;
        }
        
        // Free device-local vector index
        msi_free_vector_indices(dev, desc->hwirq, 1);
        
        // TODO: Free hwirq from tree domain
        // irq_domain_free_hwirq_range(msi_domain, desc->hwirq, 1);
        
        // Remove from list
        desc->list.next->prev = desc->list.prev;
        desc->list.prev->next = desc->list.next;
        
        msi_data->num_vectors--;
        desc->refcount--;
        if (desc->refcount == 0) {
            kfree(desc);
        }
        
        desc = next;
    }
    
    spin_unlock_irqrestore(&msi_data->lock, flags);
}

// Compose MSI message
void msi_compose_msg(struct msi_desc *desc, struct msi_msg *msg) {
    if (!desc || !msg) {
        return;
    }
    
    *msg = desc->msg;
}

// Write MSI message to descriptor
void msi_write_msg(struct msi_desc *desc, struct msi_msg *msg) {
    if (!desc || !msg) {
        return;
    }
    
    desc->msg = *msg;
}

// Mask MSI interrupt
void msi_mask_irq(struct msi_desc *desc) {
    if (!desc || !desc->irq) {
        return;
    }
    
    disable_irq_nosync(desc->irq);
}

// Unmask MSI interrupt
void msi_unmask_irq(struct msi_desc *desc) {
    if (!desc || !desc->irq) {
        return;
    }
    
    enable_irq(desc->irq);
}

// Set MSI affinity (stub for now)
int msi_set_affinity(struct msi_desc *desc, uint32_t cpu_mask) {
    (void)desc;
    (void)cpu_mask;
    // TODO: Implement when SMP support is added
    return 0;
}

// Create MSI domain (stub for now)
struct irq_domain *msi_create_domain(struct device_node *node,
                                    struct msi_domain_info *info,
                                    struct irq_domain *parent) {
    (void)node;
    (void)info;
    (void)parent;
    // TODO: Implement MSI domain hierarchy
    return NULL;
}