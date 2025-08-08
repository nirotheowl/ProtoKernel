/*
 * arch/arm64/kernel/device_types.c
 *
 * ARM64-specific device type mappings for device tree parsing
 */

#include <device/device.h>

// ARM64-specific device compatible strings
static const struct {
    const char *compatible;
    device_type_t type;
} arm64_device_map[] = {
    // CPU cores
    { "arm,cortex-a53", DEV_TYPE_CPU },
    { "arm,cortex-a57", DEV_TYPE_CPU },
    { "arm,cortex-a72", DEV_TYPE_CPU },
    { "arm,cortex-a76", DEV_TYPE_CPU },
    
    // Interrupt controllers
    { "arm,gic-v3", DEV_TYPE_INTERRUPT },
    { "arm,gic-400", DEV_TYPE_INTERRUPT },
    { "arm,cortex-a15-gic", DEV_TYPE_INTERRUPT },
    
    // Timers
    { "arm,armv8-timer", DEV_TYPE_TIMER },
    { "arm,armv7-timer", DEV_TYPE_TIMER },
    
    // UARTs
    { "arm,pl011", DEV_TYPE_UART },
    { "arm,sbsa-uart", DEV_TYPE_UART },
    
    // End marker
    { NULL, DEV_TYPE_UNKNOWN }
};

// Check if a compatible string contains a substring
static int compat_contains(const char *compat, const char *substr) {
    if (!compat || !substr) return 0;
    
    const char *p = compat;
    const char *q;
    
    while (*p) {
        q = substr;
        const char *start = p;
        
        while (*p && *q && *p == *q) {
            p++;
            q++;
        }
        
        if (!*q) return 1;  // Found match
        
        p = start + 1;
    }
    
    return 0;
}

// Architecture-specific device type detection
device_type_t arch_get_device_type(const char *compatible) {
    if (!compatible) {
        return DEV_TYPE_UNKNOWN;
    }
    
    // Check ARM64-specific mappings
    for (int i = 0; arm64_device_map[i].compatible; i++) {
        if (compat_contains(compatible, arm64_device_map[i].compatible)) {
            return arm64_device_map[i].type;
        }
    }
    
    // Check for generic ARM devices
    if (compat_contains(compatible, "arm,")) {
        // Unknown ARM device
        return DEV_TYPE_PLATFORM;
    }
    
    // Check for Rockchip devices (common on ARM64)
    if (compat_contains(compatible, "rockchip,")) {
        if (compat_contains(compatible, "uart")) {
            return DEV_TYPE_UART;
        }
        return DEV_TYPE_PLATFORM;
    }
    
    return DEV_TYPE_UNKNOWN;
}