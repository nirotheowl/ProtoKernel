#!/bin/bash

# Direct kernel boot with QEMU (for testing without U-Boot)
# This bypasses U-Boot and loads the kernel directly

KERNEL_BIN="build/kernel.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel binary not found at $KERNEL_BIN"
    echo "Please run 'make' first to build the kernel"
    exit 1
fi

echo "Starting QEMU with direct kernel boot..."
echo "Press Ctrl-A X to exit QEMU"

qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a53 \
    -m 1G \
    -nographic \
    -kernel "$KERNEL_BIN"