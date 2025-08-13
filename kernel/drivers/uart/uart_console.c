/*
 * kernel/drivers/uart/uart_console.c
 * 
 * UART console selection and auto-discovery
 * Manages console UART selection from FDT and fallback mechanisms
 */

#include <drivers/driver.h>
#include <drivers/driver_class.h>
#include <drivers/uart_drivers.h>
#include <device/device.h>
#include <device/resource.h>
#include <drivers/fdt.h>
#include <uart.h>
#include <string.h>
#include <panic.h>

// Find console UART device from FDT
static struct device *uart_find_console_device(void *fdt) {
    int chosen_offset;
    const char *fdt_console_path = NULL;
    char path[256];
    char console_path[256];
    const char *end;
    struct device *dev;
    
    if (!fdt) {
        return NULL;
    }
    
    // Find /chosen node
    chosen_offset = fdt_path_offset(fdt, "/chosen");
    if (chosen_offset < 0) {
        return NULL;
    }
    
    // Get stdout-path property (standard FDT property name for console device)
    fdt_console_path = fdt_getprop(fdt, chosen_offset, "stdout-path", NULL);
    if (!fdt_console_path) {
        fdt_console_path = fdt_getprop(fdt, chosen_offset, "linux,stdout-path", NULL);
    }
    
    if (!fdt_console_path) {
        return NULL;
    }
    
    // Make a local copy to avoid optimization issues
    strncpy(console_path, fdt_console_path, sizeof(console_path) - 1);
    console_path[sizeof(console_path) - 1] = '\0';
    
    
    // Remove any options after ':'
    end = strchr(console_path, ':');
    
    if (end) {
        size_t len = end - console_path;
        if (len >= sizeof(path)) {
            len = sizeof(path) - 1;
        }
        memcpy(path, console_path, len);
        path[len] = '\0';
    } else {
        strcpy(path, console_path);
    }
    
    // Extract device name from path
    // Handle both "/pl011@9000000" and "/soc/serial@10000000" formats
    // We want just the last component (e.g., "serial@10000000")
    const char *device_name = path;
    const char *last_slash = device_name;
    const char *p = device_name;
    
    // Find the last '/' in the path
    while (*p) {
        if (*p == '/') {
            last_slash = p;
        }
        p++;
    }
    
    // Skip the last slash if found
    if (*last_slash == '/') {
        device_name = last_slash + 1;
    }
    
    
    // Find device by name - search by name function
    dev = device_find_by_name(device_name);
    if (dev) {
        return dev;
    }
    
    return NULL;
}

// Auto-select console UART
int uart_console_auto_select(void *fdt) {
    struct device *console_dev;
    struct uart_softc *sc;
    
    
    // Try to find console device from FDT
    console_dev = uart_find_console_device(fdt);
    
    if (!console_dev) {
        // Fallback: use first UART device (active or not)
        
        console_dev = device_find_by_type(DEV_TYPE_UART);
    }
    
    if (!console_dev) {
        return -1;
    }
    
    // If device doesn't have a driver, try to probe it now
    if (!console_dev->driver) {
        if (driver_probe_device(console_dev) < 0) {
            return -1;
        }
    }
    
    // Get UART software context
    sc = uart_device_get_softc(console_dev);
    if (!sc) {
        return -1;
    }
    
    // Attach as console
    uart_console_attach(sc);
    
    
    return 0;
}

