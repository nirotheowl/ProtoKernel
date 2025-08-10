/*
 * arch/riscv/kernel/device_types.c
 * 
 * RISC-V device type mappings
 */

#include <device/device.h>
#include <string.h>

struct device_compat_entry {
    const char *compatible;
    device_type_t type;
};

static const struct device_compat_entry riscv_device_map[] = {
    // CPUs
    { "riscv", DEV_TYPE_CPU },
    { "sifive,u54-mc", DEV_TYPE_CPU },
    { "sifive,u74-mc", DEV_TYPE_CPU },
    
    // Interrupt controllers
    { "riscv,plic0", DEV_TYPE_INTERRUPT },
    { "sifive,plic-1.0.0", DEV_TYPE_INTERRUPT },
    
    // UARTs
    { "ns16550a", DEV_TYPE_UART },
    { "sifive,uart0", DEV_TYPE_UART },
    
    // Timers
    { "riscv,clint0", DEV_TYPE_TIMER },
    
    { NULL, DEV_TYPE_UNKNOWN }
};

device_type_t arch_get_device_type(const char *compatible) {
    if (!compatible) {
        return DEV_TYPE_UNKNOWN;
    }
    
    for (int i = 0; riscv_device_map[i].compatible; i++) {
        if (strstr(compatible, riscv_device_map[i].compatible)) {
            return riscv_device_map[i].type;
        }
    }
    
    return DEV_TYPE_UNKNOWN;
}