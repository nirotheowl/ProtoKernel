/*
 * arch/riscv/platform/qemu_virt.c
 * 
 * RISC-V QEMU virt machine platform support
 */

#include <memory/devmap.h>
#include <device/device.h>
#include <device/resource.h>
#include <stddef.h>

// RISC-V QEMU virt machine device mappings
static const devmap_entry_t qemu_virt_entries[] = {
    {
        .name = "UART0",
        .phys_addr = 0x10000000,
        .virt_addr = 0,  // Allocate virtual address
        .size = 0x1000,  // 4KB
        .attributes = DEVMAP_ATTR_DEVICE
    },
    {
        .name = "PLIC",
        .phys_addr = 0x0c000000,
        .virt_addr = 0,
        .size = 0x4000000,
        .attributes = DEVMAP_ATTR_DEVICE
    },
    // Terminator
    { .size = 0 }
};

static const platform_devmap_t qemu_virt_platform_devmap = {
    .platform_name = "RISC-V QEMU virt",
    .entries = qemu_virt_entries
};

// Platform detection
static int qemu_virt_detect(void) {
    // Always match for now - could check FDT compatible string
    return 1;
}

// Platform descriptor
const platform_desc_t qemu_virt_platform = {
    .name = "RISC-V QEMU virt",
    .detect = qemu_virt_detect,
    .devmap = &qemu_virt_platform_devmap,
    .console_uart_phys = 0x10000000,
    .console_uart_compatible = "ns16550a"
};

// Stub for odroid - not supported on RISC-V
static const platform_devmap_t empty_devmap = {
    .platform_name = "Not supported",
    .entries = NULL
};

const platform_desc_t odroid_m2_platform = {
    .name = "Not supported on RISC-V",
    .detect = NULL,
    .devmap = &empty_devmap,
    .console_uart_phys = 0,
    .console_uart_compatible = NULL
};