/*
 * kernel/platform/odroid_m2.c
 *
 * Platform support for ODroid-M2 (Rockchip RK3588S)
 */

#include <platform/devmap.h>
#include <stdint.h>
#include <stddef.h>

/* ODroid-M2 / Rockchip RK3588S device memory map - MINIMAL for early boot only */
static const devmap_entry_t odroid_m2_devmap[] = {
    /* UART2 - Debug console - Required for early console */
    {
        .name = "UART2",
        .phys_addr = 0xFEB50000,
        .virt_addr = 0,
        .size = 0x1000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* Terminator */
    { .size = 0 }
};

static const platform_devmap_t odroid_m2_platform_devmap = {
    .platform_name = "ODroid-M2",
    .entries = odroid_m2_devmap
};

/* Platform detection for ODroid-M2 */
static int odroid_m2_detect(void)
{
    /* TODO: Implement actual detection logic */
    /* Check DTB compatible string for "rockchip,rk3588s" or "hardkernel,odroid-m2" */
    /* This is a placeholder */
    return 0;
}

/* Platform descriptor */
const platform_desc_t odroid_m2_platform = {
    .name = "ODroid-M2",
    .detect = odroid_m2_detect,
    .devmap = &odroid_m2_platform_devmap,
    .console_uart_phys = 0xFEB50000,    /* UART2 - Debug console */
    .console_uart_compatible = "rockchip,rk3588-uart" /* RK3588S UART */
};