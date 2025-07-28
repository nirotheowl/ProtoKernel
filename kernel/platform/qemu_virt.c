/*
 * kernel/platform/qemu_virt.c
 *
 * Platform support for QEMU virt machine (AArch64)
 */

#include <platform/devmap.h>
#include <stdint.h>
#include <stddef.h>

/* QEMU virt machine device memory map - MINIMAL for early boot only */
static const devmap_entry_t qemu_virt_devmap[] = {
    /* PL011 UART - Required for early console */
    {
        .name = "UART0",
        .phys_addr = 0x09000000,
        .virt_addr = 0,  /* Allocate virtual address */
        .size = 0x1000,  /* 4KB */
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
    .devmap = &qemu_virt_platform_devmap,
    .console_uart_phys = 0x09000000,    /* PL011 UART0 */
    .console_uart_compatible = "arm,pl011"
};
