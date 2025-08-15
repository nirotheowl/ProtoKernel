/*
 * kernel/include/drivers/driver_module.h
 * 
 * Automatic driver module initialization system using linker sections
 */

#ifndef __DRIVER_MODULE_H
#define __DRIVER_MODULE_H

#include <stddef.h>

// Driver initialization function type
typedef void (*driver_init_fn)(void);

// Priority levels for driver initialization order
enum driver_priority {
    DRIVER_PRIO_EARLY = 100,    // Early initialization drivers
    DRIVER_PRIO_NORMAL = 500,   // Normal priority drivers
    DRIVER_PRIO_LATE = 900,     // Late initialization drivers
};

// Driver module initialization entry
struct driver_init_entry {
    driver_init_fn init;
    int priority;
    const char *name;
};

// UART driver module macro
// Places driver init function in special linker section for automatic initialization
// Priority determines initialization order (lower runs first)
#define UART_DRIVER_MODULE(initfn, prio) \
    static const struct driver_init_entry __uart_driver_##initfn \
    __attribute__((used, section(".uart_drivers"), aligned(8))) = { \
        .init = initfn, \
        .priority = prio, \
        .name = #initfn \
    }

// Generic driver module macro for future subsystems
#define DRIVER_MODULE(subsys, initfn, prio) \
    static const struct driver_init_entry __##subsys##_driver_##initfn \
    __attribute__((used, section("." #subsys "_drivers"), aligned(8))) = { \
        .init = initfn, \
        .priority = prio, \
        .name = #initfn \
    }

// IRQCHIP driver module macro
// For interrupt controller drivers - these need to initialize very early
#define IRQCHIP_DRIVER_MODULE(initfn, prio) \
    static const struct driver_init_entry __irqchip_driver_##initfn \
    __attribute__((used, section(".irqchip_drivers"), aligned(8))) = { \
        .init = initfn, \
        .priority = prio, \
        .name = #initfn \
    }

// Linker-provided symbols for driver sections
extern struct driver_init_entry __uart_drivers_start[];
extern struct driver_init_entry __uart_drivers_end[];
extern struct driver_init_entry __irqchip_drivers_start[];
extern struct driver_init_entry __irqchip_drivers_end[];

// Helper to iterate and initialize driver modules
static inline void driver_module_init_uart(void) {
    struct driver_init_entry *entry;
    
    // Simple iteration - could add priority sorting later
    for (entry = __uart_drivers_start; entry < __uart_drivers_end; entry++) {
        if (entry->init) {
            entry->init();
        }
    }
}

static inline void driver_module_init_irqchip(void) {
    struct driver_init_entry *entry;
    
    // Simple iteration - could add priority sorting later
    for (entry = __irqchip_drivers_start; entry < __irqchip_drivers_end; entry++) {
        if (entry->init) {
            entry->init();
        }
    }
}

#endif /* __DRIVER_MODULE_H */