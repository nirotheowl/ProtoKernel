/*
 * kernel/platform/qemu_virt.c
 *
 * Platform support for QEMU virt machine (AArch64)
 */

#include <memory/devmap.h>
#include <stdint.h>
#include <stddef.h>
#include <device/device.h>
#include <device/resource.h>

// QEMU virt machine device memory map - MINIMAL for early boot only
static const devmap_entry_t qemu_virt_devmap[] = {
    // PL011 UART - Required for early console
    {
        .name = "UART0",
        .phys_addr = 0x09000000,
        .virt_addr = 0,  // Allocate virtual address
        .size = 0x1000,  // 4KB
        .attributes = DEVMAP_ATTR_DEVICE
    },
    // Terminator
    { .size = 0 }
};

static const platform_devmap_t qemu_virt_platform_devmap = {
    .platform_name = "QEMU virt",
    .entries = qemu_virt_devmap
};

// Platform detection for QEMU virt
static int qemu_virt_detect(void)
{
    // Check if this is QEMU virt platform by looking for specific devices
    // or compatible strings in the device tree
    
    // For now, check if we can find the PL011 UART at the expected address
    struct device *uart = device_find_by_compatible("arm,pl011");
    if (uart) {
        struct resource *res = device_get_resource(uart, RES_TYPE_MEM, 0);
        if (res && res->start == 0x09000000) {
            return 1;  // This is likely QEMU virt
        }
    }
    
    return 0;
}

// Platform descriptor
const platform_desc_t qemu_virt_platform = {
    .name = "QEMU virt",
    .detect = qemu_virt_detect,
    .devmap = &qemu_virt_platform_devmap,
    .console_uart_phys = 0x09000000,    // PL011 UART0
    .console_uart_compatible = "arm,pl011"
};
