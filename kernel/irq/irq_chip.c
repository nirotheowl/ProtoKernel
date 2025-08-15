#include <irq/irq_domain.h>
#include <irq/irq.h>
#include <string.h>

// Default chip operations (no-op implementations)
static void default_irq_enable(struct irq_desc *desc) {
    // Default: unmask the interrupt
    if (desc->chip && desc->chip->irq_unmask) {
        desc->chip->irq_unmask(desc);
    }
}

static void default_irq_disable(struct irq_desc *desc) {
    // Default: mask the interrupt
    if (desc->chip && desc->chip->irq_mask) {
        desc->chip->irq_mask(desc);
    }
}

static void default_irq_ack(struct irq_desc *desc) {
    // Default: no-op for level interrupts
}

static void default_irq_mask(struct irq_desc *desc) {
    // Default: set masked flag
    desc->status |= IRQ_MASKED;
}

static void default_irq_unmask(struct irq_desc *desc) {
    // Default: clear masked flag
    desc->status &= ~IRQ_MASKED;
}

static void default_irq_eoi(struct irq_desc *desc) {
    // Default: no-op for edge interrupts
}

// Set default operations for a chip
void irq_chip_set_defaults(struct irq_chip *chip) {
    if (!chip) {
        return;
    }
    
    if (!chip->irq_enable) {
        chip->irq_enable = default_irq_enable;
    }
    if (!chip->irq_disable) {
        chip->irq_disable = default_irq_disable;
    }
    if (!chip->irq_ack) {
        chip->irq_ack = default_irq_ack;
    }
    if (!chip->irq_mask) {
        chip->irq_mask = default_irq_mask;
    }
    if (!chip->irq_unmask) {
        chip->irq_unmask = default_irq_unmask;
    }
    if (!chip->irq_eoi) {
        chip->irq_eoi = default_irq_eoi;
    }
}

// Generic interrupt handler dispatch
void generic_handle_irq(uint32_t irq) {
    struct irq_desc *desc;
    struct irq_action *action;
    unsigned long flags;
    
    desc = irq_to_desc(irq);
    if (!desc) {
        return;
    }
    
    spin_lock_irqsave(&desc->lock, flags);
    
    // Check if interrupt is disabled
    if (desc->status & IRQ_DISABLED) {
        spin_unlock_irqrestore(&desc->lock, flags);
        return;
    }
    
    // Mark as in progress
    desc->status |= IRQ_INPROGRESS;
    
    // Acknowledge the interrupt if needed
    if (desc->chip && desc->chip->irq_ack) {
        desc->chip->irq_ack(desc);
    }
    
    // Update statistics
    desc->count++;
    
    // Get action chain
    action = desc->action;
    
    spin_unlock_irqrestore(&desc->lock, flags);
    
    // Call all handlers in the chain
    while (action) {
        if (action->handler) {
            action->handler(action->dev_data);
        }
        action = action->next;
    }
    
    spin_lock_irqsave(&desc->lock, flags);
    
    // Clear in progress
    desc->status &= ~IRQ_INPROGRESS;
    
    // Send EOI if needed
    if (desc->chip && desc->chip->irq_eoi) {
        desc->chip->irq_eoi(desc);
    }
    
    spin_unlock_irqrestore(&desc->lock, flags);
}

// Handle domain interrupt
void irq_domain_handle_irq(struct irq_domain *domain, uint32_t hwirq) {
    uint32_t virq;
    
    if (!domain) {
        return;
    }
    
    // Find virtual IRQ for this hardware IRQ
    virq = irq_find_mapping(domain, hwirq);
    if (virq == IRQ_INVALID) {
        // No mapping found - spurious interrupt
        return;
    }
    
    // Dispatch to generic handler
    generic_handle_irq(virq);
}