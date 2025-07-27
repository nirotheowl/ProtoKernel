/*
 * kernel/platform/qemu_virt.c
 *
 * Platform support for QEMU virt machine (AArch64)
 */

#include <platform/devmap.h>
#include <stdint.h>
#include <stddef.h>

/* QEMU virt machine device memory map */
static const devmap_entry_t qemu_virt_devmap[] = {
    /* GIC v3 Distributor */
    {
        .name = "GIC_DIST",
        .phys_addr = 0x08000000,
        .virt_addr = 0,  /* Allocate virtual address */
        .size = 0x10000,  /* 64KB */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* GIC v3 Redistributor */
    {
        .name = "GIC_REDIST",
        .phys_addr = 0x080A0000,
        .virt_addr = 0,
        .size = 0xF60000,  /* Up to 123 CPUs */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* PL011 UART */
    {
        .name = "UART0",
        .phys_addr = 0x09000000,
        .virt_addr = 0,
        .size = 0x1000,  /* 4KB */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* RTC */
    {
        .name = "RTC",
        .phys_addr = 0x09010000,
        .virt_addr = 0,
        .size = 0x1000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* GPIO */
    {
        .name = "GPIO",
        .phys_addr = 0x09030000,
        .virt_addr = 0,
        .size = 0x1000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* Secure UART */
    {
        .name = "SECURE_UART",
        .phys_addr = 0x09040000,
        .virt_addr = 0,
        .size = 0x1000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* SMMU v3 */
    {
        .name = "SMMU",
        .phys_addr = 0x09050000,
        .virt_addr = 0,
        .size = 0x20000,  /* 128KB */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* Virtio MMIO devices (32 slots) */
    {
        .name = "VIRTIO_MMIO",
        .phys_addr = 0x0A000000,
        .virt_addr = 0,
        .size = 0x00200000,  /* 32 * 512 bytes with 4KB spacing */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* Platform bus */
    {
        .name = "PLATFORM_BUS",
        .phys_addr = 0x0C000000,
        .virt_addr = 0,
        .size = 0x02000000,  /* 32MB */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* PCI ECAM */
    {
        .name = "PCI_ECAM",
        .phys_addr = 0x3F000000,
        .virt_addr = 0,
        .size = 0x01000000,  /* 16MB */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* PCI MMIO space */
    {
        .name = "PCI_MMIO",
        .phys_addr = 0x10000000,
        .virt_addr = 0,
        .size = 0x2EFF0000,  /* ~751MB */
        .attributes = DEVMAP_ATTR_DEVICE
    },
    /* Terminator */
    { .size = 0 }
};

static const platform_devmap_t qemu_virt_platform_devmap = {
    .platform_name = "QEMU virt",
    .entries = qemu_virt_devmap
};

/* Platform detection for QEMU virt */
static int qemu_virt_detect(void)
{
    /* TODO: Implement actual detection logic */
    /* For now, we'll check for QEMU-specific features or DTB compatible string */
    /* This is a placeholder that always returns true for QEMU development */
    return 1;
}

/* Platform descriptor */
const platform_desc_t qemu_virt_platform = {
    .name = "QEMU virt",
    .detect = qemu_virt_detect,
    .devmap = &qemu_virt_platform_devmap
};
