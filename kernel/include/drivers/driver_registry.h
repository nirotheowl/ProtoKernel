/*
 * kernel/include/drivers/driver_registry.h
 * 
 * Automatic driver registration system using linker sections
 */

#ifndef __DRIVER_REGISTRY_H
#define __DRIVER_REGISTRY_H

#include <stddef.h>

// Driver initialization function type
typedef void (*driver_init_fn)(void);

// Priority levels for driver initialization order
enum driver_priority {
    DRIVER_PRIO_EARLY = 100,    // Early initialization drivers
    DRIVER_PRIO_NORMAL = 500,   // Normal priority drivers
    DRIVER_PRIO_LATE = 900,     // Late initialization drivers
};

// Driver registration entry
struct driver_init_entry {
    driver_init_fn init;
    int priority;
    const char *name;
};

// UART driver registration macro
// Places driver init function in special linker section
// Priority determines initialization order (lower runs first)
#define UART_DRIVER_REGISTER(initfn, prio) \
    static const struct driver_init_entry __uart_driver_##initfn \
    __attribute__((used, section(".uart_drivers"), aligned(8))) = { \
        .init = initfn, \
        .priority = prio, \
        .name = #initfn \
    }

// Generic driver registration for future subsystems
#define DRIVER_REGISTER(subsys, initfn, prio) \
    static const struct driver_init_entry __##subsys##_driver_##initfn \
    __attribute__((used, section("." #subsys "_drivers"), aligned(8))) = { \
        .init = initfn, \
        .priority = prio, \
        .name = #initfn \
    }

// Linker-provided symbols for driver sections
extern struct driver_init_entry __uart_drivers_start[];
extern struct driver_init_entry __uart_drivers_end[];

// Helper to iterate and initialize drivers
static inline void driver_registry_init_uart(void) {
    struct driver_init_entry *entry;
    
    // Simple iteration - could add priority sorting later
    for (entry = __uart_drivers_start; entry < __uart_drivers_end; entry++) {
        if (entry->init) {
            entry->init();
        }
    }
}

#endif /* __DRIVER_REGISTRY_H */