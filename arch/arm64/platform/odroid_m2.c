/*
 * arch/arm64/platform/odroid_m2.c
 *
 * Platform support for ODroid-M2 (Rockchip RK3588S)
 */

#include <memory/devmap.h>
#include <stdint.h>
#include <stddef.h>
#include <device/device.h>
#include <device/resource.h>

// ODroid-M2 / Rockchip RK3588S device memory map - MINIMAL for early boot only
static const devmap_entry_t odroid_m2_devmap[] = {
    // UART2 - Debug console - Required for early console
    {
        .name = "UART2",
        .phys_addr = 0xFEB50000,
        .virt_addr = 0,
        .size = 0x1000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    // Terminator
    { .size = 0 }
};

static const platform_devmap_t odroid_m2_platform_devmap = {
    .platform_name = "ODroid-M2",
    .entries = odroid_m2_devmap
};

// Platform detection for ODroid-M2
static int odroid_m2_detect(void)
{
    /* Check if this is ODroid-M2 platform by looking for specific devices
     * or compatible strings in the device tree */
    
    // Check for Rockchip UART at the expected address
    struct device *uart = device_find_by_compatible("rockchip,rk3588-uart");
    if (uart) {
        struct resource *res = device_get_resource(uart, RES_TYPE_MEM, 0);
        if (res && res->start == 0xFEB50000) {
            return 1;  // This is likely ODroid-M2
        }
    }
    
    return 0;
}

// Platform descriptor
const platform_desc_t odroid_m2_platform = {
    .name = "ODroid-M2",
    .detect = odroid_m2_detect,
    .devmap = &odroid_m2_platform_devmap,
    .console_uart_phys = 0xFEB50000,    // UART2 - Debug console
    .console_uart_compatible = "rockchip,rk3588-uart" // RK3588S UART
};