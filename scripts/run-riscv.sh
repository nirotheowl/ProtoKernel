#!/bin/bash

# Direct kernel boot with QEMU (for testing without U-Boot)
# This bypasses U-Boot and loads the kernel directly

KERNEL_BIN="build/riscv/kernel.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel binary not found at $KERNEL_BIN"
    echo "Please run 'make ARCH=riscv' first to build the kernel"
    exit 1
fi

echo "Starting QEMU with direct kernel boot..."
echo "Press Ctrl-A X to exit QEMU"

# Using aia=aplic-imsic to test with MSI-capable hardware
# The APLIC driver will configure it for direct mode until MSI is implemented
qemu-system-riscv64 \
    -M virt,aia=aplic-imsic \
    -bios default \
    -m 1G \
    -nographic \
    -kernel "$KERNEL_BIN"
