#!/bin/bash

# Run QEMU with a timeout to see if kernel continues after MMU enable
# This helps debug if the kernel is running but UART output stopped

KERNEL_BIN="build/kernel.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel binary not found at $KERNEL_BIN"
    echo "Please run 'make' first to build the kernel"
    exit 1
fi

echo "Starting QEMU with 10 second timeout..."
echo "This will help determine if kernel continues after MMU enable"
echo ""

# Run with timeout
timeout 10s qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a53 \
    -m 1G \
    -nographic \
    -kernel "$KERNEL_BIN"

EXIT_CODE=$?

if [ $EXIT_CODE -eq 124 ]; then
    echo ""
    echo "QEMU timed out after 10 seconds - kernel may still be running!"
    echo "This suggests MMU is enabled but UART output stopped working."
else
    echo ""
    echo "QEMU exited with code: $EXIT_CODE"
fi