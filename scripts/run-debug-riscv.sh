#!/bin/bash

# Run kernel with QEMU in debug mode

KERNEL_BIN="build/riscv/kernel.bin"
KERNEL_ELF="build/riscv/kernel.elf"
DEBUG_PORT=1234

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel binary not found at $KERNEL_BIN"
    echo "Please run 'make ARCH=riscv' first to build the kernel"
    exit 1
fi

echo "Starting QEMU in debug mode..."
echo "GDB will listen on port $DEBUG_PORT"
echo "Press Ctrl-A X to exit QEMU"
echo ""
echo "To connect with GDB, run in another terminal:"
echo "  ./scripts/debug-gdb-riscv.sh"
echo ""

# Use binary file for proper DTB loading, GDB will load symbols from ELF
qemu-system-riscv64 \
    -M virt \
    -bios default \
    -m 1G \
    -nographic \
    -kernel "$KERNEL_BIN" \
    -S \
    -gdb tcp::$DEBUG_PORT
