#!/bin/bash

# Run kernel with QEMU in debug mode

KERNEL_ELF="build/kernel.elf"
DEBUG_PORT=1234

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: Kernel ELF not found at $KERNEL_ELF"
    echo "Please run 'make' first to build the kernel"
    exit 1
fi

echo "Starting QEMU in debug mode..."
echo "GDB will listen on port $DEBUG_PORT"
echo "Press Ctrl-A X to exit QEMU"
echo ""
echo "To connect with GDB, run in another terminal:"
echo "  ./scripts/debug-gdb.sh"
echo ""

# Use ELF file instead of binary for proper debugging with symbols
qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a53 \
    -m 1G \
    -nographic \
    -kernel "$KERNEL_ELF" \
    -S \
    -gdb tcp::$DEBUG_PORT