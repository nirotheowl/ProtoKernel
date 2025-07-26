/*
 * kernel/platform/odroid_m2.c
 *
 * Platform support for ODroid-M2 (Rockchip RK3588S)
 */

#include <platform/devmap.h>
#include <stdint.h>
#include <stddef.h>

/* ODroid-M2 / Rockchip RK3588S device memory map */
static const devmap_entry_t odroid_m2_devmap[] = {
    /* GIC-600 (GICv3) */
    {
        .name = "GIC_DIST",
        .phys_addr = 0xFE600000,
        .virt_addr = 0,
        .size = 0x10000,  /* 64KB */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    {
        .name = "GIC_REDIST",
        .phys_addr = 0xFE680000,
        .virt_addr = 0,
        .size = 0x100000,  /* 1MB for 8 cores */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* UART2 - Debug console */
    {
        .name = "UART2",
        .phys_addr = 0xFEB50000,
        .virt_addr = 0,
        .size = 0x1000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* CRU (Clock & Reset Unit) */
    {
        .name = "CRU",
        .phys_addr = 0xFD7C0000,
        .virt_addr = 0,
        .size = 0x40000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* PMU CRU */
    {
        .name = "PMU_CRU",
        .phys_addr = 0xFD7F0000,
        .virt_addr = 0,
        .size = 0x10000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* System GRF (General Register Files) */
    {
        .name = "SYS_GRF",
        .phys_addr = 0xFD58C000,
        .virt_addr = 0,
        .size = 0x1000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* GPIO banks (5 banks) */
    {
        .name = "GPIO0",
        .phys_addr = 0xFD8A0000,
        .virt_addr = 0,
        .size = 0x10000,  /* All 5 banks */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* Timer */
    {
        .name = "TIMER",
        .phys_addr = 0xFEAE0000,
        .virt_addr = 0,
        .size = 0x20000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* PCIe 3.0 Controller 0 */
    {
        .name = "PCIE30_0",
        .phys_addr = 0xFE150000,
        .virt_addr = 0,
        .size = 0x10000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* PCIe 3.0 Controller 1 */
    {
        .name = "PCIE30_1",
        .phys_addr = 0xFE160000,
        .virt_addr = 0,
        .size = 0x10000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* PCIe 2.0 Controller */
    {
        .name = "PCIE20",
        .phys_addr = 0xFE170000,
        .virt_addr = 0,
        .size = 0x10000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* USB3 Host Controllers */
    {
        .name = "USB3_HOST0",
        .phys_addr = 0xFC000000,
        .virt_addr = 0,
        .size = 0x400000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    {
        .name = "USB3_HOST1",
        .phys_addr = 0xFC400000,
        .virt_addr = 0,
        .size = 0x400000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* GMAC (Gigabit Ethernet) Controllers */
    {
        .name = "GMAC0",
        .phys_addr = 0xFE1B0000,
        .virt_addr = 0,
        .size = 0x10000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    {
        .name = "GMAC1",
        .phys_addr = 0xFE1C0000,
        .virt_addr = 0,
        .size = 0x10000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* eMMC/SD Controllers */
    {
        .name = "SDHCI",
        .phys_addr = 0xFE2E0000,
        .virt_addr = 0,
        .size = 0x10000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* SPI Controllers */
    {
        .name = "SPI0",
        .phys_addr = 0xFEB00000,
        .virt_addr = 0,
        .size = 0x1000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* I2C Controllers */
    {
        .name = "I2C0",
        .phys_addr = 0xFEA90000,
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
    .devmap = &odroid_m2_platform_devmap
};