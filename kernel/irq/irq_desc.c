#include <irq/irq.h>
#include <irq/irq_domain.h>
#include <irq/irq_alloc.h>
#include <memory/kmalloc.h>
#include <memory/slab.h>
#include <string.h>
#include <panic.h>

#define MAX_IRQ_DESC    1024

// Static descriptor array
static struct irq_desc *irq_desc_array[MAX_IRQ_DESC];
static spinlock_t irq_desc_lock = SPINLOCK_INITIALIZER;

// Statistics
static uint32_t irq_desc_allocated = 0;
static uint32_t irq_desc_peak = 0;

// Initialize IRQ subsystem
void irq_init(void) {
    unsigned long flags;
    
    spin_lock_irqsave(&irq_desc_lock, flags);
    
    // Clear descriptor array
    memset(irq_desc_array, 0, sizeof(irq_desc_array));
    
    // Initialize the virtual IRQ allocator
    virq_allocator_init();
    
    irq_desc_allocated = 0;
    irq_desc_peak = 0;
    
    spin_unlock_irqrestore(&irq_desc_lock, flags);
}

// Get IRQ descriptor by virtual IRQ number
struct irq_desc *irq_to_desc(uint32_t irq) {
    unsigned long flags;
    struct irq_desc *desc = NULL;
    
    if (irq >= MAX_IRQ_DESC) {
        return NULL;
    }
    
    spin_lock_irqsave(&irq_desc_lock, flags);
    desc = irq_desc_array[irq];
    spin_unlock_irqrestore(&irq_desc_lock, flags);
    
    return desc;
}

// Allocate a new IRQ descriptor
struct irq_desc *irq_desc_alloc(uint32_t irq) {
    unsigned long flags;
    struct irq_desc *desc;
    
    if (irq >= MAX_IRQ_DESC) {
        return NULL;
    }
    
    spin_lock_irqsave(&irq_desc_lock, flags);
    
    // Check if already allocated
    if (irq_desc_array[irq] != NULL) {
        spin_unlock_irqrestore(&irq_desc_lock, flags);
        return irq_desc_array[irq];
    }
    
    // Allocate new descriptor
    desc = kmalloc(sizeof(struct irq_desc), KM_ZERO);
    if (!desc) {
        spin_unlock_irqrestore(&irq_desc_lock, flags);
        return NULL;
    }
    
    // Initialize descriptor (already zeroed by KM_ZERO)
    desc->irq = irq;
    desc->hwirq = IRQ_INVALID;
    desc->domain = NULL;
    desc->parent_desc = NULL;
    desc->chip = NULL;
    desc->chip_data = NULL;
    desc->action = NULL;
    desc->status = IRQ_DISABLED;
    desc->trigger_type = IRQ_TYPE_NONE;
    desc->cpu_mask = 0xFFFFFFFF;  // All CPUs
    desc->depth = 1;  // Start disabled
    desc->count = 0;
    desc->spurious_count = 0;
    desc->last_timestamp = 0;
    spin_lock_init(&desc->lock);
    desc->name = NULL;
    
    // Store in array
    irq_desc_array[irq] = desc;
    
    // Update statistics
    irq_desc_allocated++;
    if (irq_desc_allocated > irq_desc_peak) {
        irq_desc_peak = irq_desc_allocated;
    }
    
    spin_unlock_irqrestore(&irq_desc_lock, flags);
    
    return desc;
}

// Free an IRQ descriptor
void irq_desc_free(struct irq_desc *desc) {
    unsigned long flags;
    uint32_t irq;
    
    if (!desc) {
        return;
    }
    
    irq = desc->irq;
    if (irq >= MAX_IRQ_DESC) {
        return;
    }
    
    spin_lock_irqsave(&irq_desc_lock, flags);
    
    // Verify this is the correct descriptor
    if (irq_desc_array[irq] != desc) {
        spin_unlock_irqrestore(&irq_desc_lock, flags);
        return;
    }
    
    // Check if there are still handlers attached
    if (desc->action != NULL) {
        spin_unlock_irqrestore(&irq_desc_lock, flags);
        panic("Attempting to free IRQ descriptor with handlers still attached");
        return;
    }
    
    // Remove from array
    irq_desc_array[irq] = NULL;
    irq_desc_allocated--;
    
    spin_unlock_irqrestore(&irq_desc_lock, flags);
    
    // Free memory
    kfree(desc);
}

// Request an interrupt handler
int request_irq(uint32_t irq, irq_handler_t handler,
               unsigned long flags, const char *name, void *dev) {
    struct irq_desc *desc;
    struct irq_action *action, *old, **p;
    unsigned long irqflags;
    
    if (irq >= MAX_IRQ_DESC || !handler) {
        return -1;
    }
    
    desc = irq_to_desc(irq);
    if (!desc) {
        return -1;
    }
    
    // Allocate new action
    action = kmalloc(sizeof(struct irq_action), 0);
    if (!action) {
        return -1;
    }
    
    action->handler = handler;
    action->flags = flags;
    action->name = name;
    action->dev_data = dev;
    action->next = NULL;
    
    spin_lock_irqsave(&desc->lock, irqflags);
    
    // Check if shared interrupt
    if (desc->action != NULL) {
        if (!(desc->action->flags & IRQF_SHARED) || !(flags & IRQF_SHARED)) {
            spin_unlock_irqrestore(&desc->lock, irqflags);
            kfree(action);
            return -1;  // Cannot share
        }
        
        // Add to end of chain
        p = &desc->action;
        while (*p) {
            old = *p;
            p = &old->next;
        }
        *p = action;
    } else {
        // First handler for this IRQ
        desc->action = action;
        desc->status &= ~IRQ_DISABLED;
        desc->depth = 0;  // Enable the IRQ
        
        // Set trigger type if specified
        if (flags & IRQF_TRIGGER_RISING) {
            desc->trigger_type = IRQ_TYPE_EDGE_RISING;
        } else if (flags & IRQF_TRIGGER_FALLING) {
            desc->trigger_type = IRQ_TYPE_EDGE_FALLING;
        } else if (flags & IRQF_TRIGGER_HIGH) {
            desc->trigger_type = IRQ_TYPE_LEVEL_HIGH;
        } else if (flags & IRQF_TRIGGER_LOW) {
            desc->trigger_type = IRQ_TYPE_LEVEL_LOW;
        }
        
        // Enable the interrupt if we have a chip
        if (desc->chip && desc->chip->irq_unmask) {
            desc->chip->irq_unmask(desc);
        }
    }
    
    spin_unlock_irqrestore(&desc->lock, irqflags);
    
    return 0;
}

// Free an interrupt handler
void free_irq(uint32_t irq, void *dev) {
    struct irq_desc *desc;
    struct irq_action **p, *action;
    unsigned long flags;
    
    if (irq >= MAX_IRQ_DESC) {
        return;
    }
    
    desc = irq_to_desc(irq);
    if (!desc) {
        return;
    }
    
    spin_lock_irqsave(&desc->lock, flags);
    
    // Find and remove the action
    p = &desc->action;
    while (*p) {
        action = *p;
        
        if (action->dev_data == dev) {
            *p = action->next;
            
            // If this was the last handler, disable the interrupt
            if (desc->action == NULL) {
                desc->status |= IRQ_DISABLED;
                desc->depth = 1;  // Disable the IRQ
                if (desc->chip && desc->chip->irq_mask) {
                    desc->chip->irq_mask(desc);
                }
            }
            
            spin_unlock_irqrestore(&desc->lock, flags);
            kfree(action);
            return;
        }
        
        p = &action->next;
    }
    
    spin_unlock_irqrestore(&desc->lock, flags);
}

// Enable an interrupt
void enable_irq(uint32_t irq) {
    struct irq_desc *desc;
    unsigned long flags;
    
    desc = irq_to_desc(irq);
    if (!desc) {
        return;
    }
    
    spin_lock_irqsave(&desc->lock, flags);
    
    if (desc->depth > 0) {
        desc->depth--;
        if (desc->depth == 0) {
            desc->status &= ~IRQ_DISABLED;
            if (desc->chip && desc->chip->irq_unmask) {
                desc->chip->irq_unmask(desc);
            }
        }
    }
    
    spin_unlock_irqrestore(&desc->lock, flags);
}

// Disable an interrupt (wait for handlers to complete)
void disable_irq(uint32_t irq) {
    struct irq_desc *desc;
    unsigned long flags;
    
    desc = irq_to_desc(irq);
    if (!desc) {
        return;
    }
    
    spin_lock_irqsave(&desc->lock, flags);
    
    if (desc->depth == 0) {
        desc->status |= IRQ_DISABLED;
        if (desc->chip && desc->chip->irq_mask) {
            desc->chip->irq_mask(desc);
        }
    }
    desc->depth++;
    
    spin_unlock_irqrestore(&desc->lock, flags);
    
    // Wait for any in-progress handlers to complete
    while (desc->status & IRQ_INPROGRESS) {
        // Spin wait - in real implementation would yield CPU
    }
}

// Disable an interrupt without waiting
void disable_irq_nosync(uint32_t irq) {
    struct irq_desc *desc;
    unsigned long flags;
    
    desc = irq_to_desc(irq);
    if (!desc) {
        return;
    }
    
    spin_lock_irqsave(&desc->lock, flags);
    
    if (desc->depth == 0) {
        desc->status |= IRQ_DISABLED;
        if (desc->chip && desc->chip->irq_mask) {
            desc->chip->irq_mask(desc);
        }
    }
    desc->depth++;
    
    spin_unlock_irqrestore(&desc->lock, flags);
}