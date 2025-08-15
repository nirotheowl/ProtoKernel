/*
 * kernel/drivers/irqchip/irqchip.c
 * 
 * Interrupt controller driver registration and initialization
 */

#include <drivers/driver_module.h>
#include <drivers/driver.h>
#include <device/device.h>
#include <uart.h>

// Callback for device_for_each_child to probe interrupt controllers
static int irqchip_probe_callback(struct device *dev, void *data) {
    int *count = (int *)data;
    
    // Check if this device is an interrupt controller
    if (dev->type == DEV_TYPE_INTERRUPT) {
        uart_puts("IRQCHIP: Found interrupt controller: ");
        uart_puts(dev->name);
        uart_puts("\n");
        
        if (driver_probe_device(dev) >= 0) {
            uart_puts("IRQCHIP: Successfully probed ");
            uart_puts(dev->name);
            uart_puts("\n");
            (*count)++;
        }
    }
    
    // Recursively check children
    device_for_each_child(dev, irqchip_probe_callback, data);
    
    return 0; // Continue iteration
}

// Probe all interrupt controller devices
static void irqchip_probe_all(void) {
    struct device *root;
    int count = 0;
    
    uart_puts("IRQCHIP: Probing interrupt controller devices\n");
    
    root = device_get_root();
    if (!root) {
        uart_puts("IRQCHIP: No device tree available\n");
        return;
    }
    
    // Start recursive probe from root
    irqchip_probe_callback(root, &count);
    
    if (count == 0) {
        uart_puts("IRQCHIP: Warning - no interrupt controllers found or probed\n");
    } else {
        uart_puts("IRQCHIP: Probed ");
        uart_putdec(count);
        uart_puts(" interrupt controller(s)\n");
    }
}

// Initialize all interrupt controller drivers
void irqchip_init(void) {
    uart_puts("IRQCHIP: Initializing interrupt controller drivers\n");
    
    // Automatically register all IRQCHIP drivers from linker section
    driver_module_init_irqchip();
    
    // Probe all interrupt controller devices
    irqchip_probe_all();
    
    uart_puts("IRQCHIP: Driver initialization complete\n");
}