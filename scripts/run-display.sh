#!/bin/bash

# Run kernel with QEMU and display output

KERNEL_BIN="build/kernel.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel binary not found at $KERNEL_BIN"
    echo "Please run 'make' first to build the kernel"
    exit 1
fi

echo "Press Ctrl-Alt-G to release mouse grab"

qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a53 \
    -m 1G \
    -kernel "$KERNEL_BIN" \
    -serial stdio \
    -device virtio-gpu \
    -display sdl
