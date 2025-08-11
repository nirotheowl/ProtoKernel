#!/bin/bash

# QEMU Debug Script for RISC-V
# Usage: ./debug-qemu-riscv.sh <gdb-script>
#
# This script:
# 1. Launches QEMU in debug mode with the kernel binary
# 2. Connects GDB with the provided script
# 3. Loads symbols from the ELF file

# Check if GDB script argument is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 <gdb-script>"
    echo "Example: $0 debug-boot.gdb"
    exit 1
fi

GDB_SCRIPT="$1"

# Check if GDB script exists
if [ ! -f "$GDB_SCRIPT" ]; then
    echo "Error: GDB script '$GDB_SCRIPT' not found"
    exit 1
fi

# Define paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build/riscv"
KERNEL_BIN="$BUILD_DIR/kernel.bin"
KERNEL_ELF="$BUILD_DIR/kernel.elf"

# Check if kernel binary exists
if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel binary not found at $KERNEL_BIN"
    echo "Please build the kernel first with 'make ARCH=riscv'"
    exit 1
fi

# Check if kernel ELF exists (for symbols)
if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: Kernel ELF not found at $KERNEL_ELF"
    echo "Please build the kernel first with 'make ARCH=riscv'"
    exit 1
fi

# Kill any existing QEMU instances
pkill -f "qemu-system-riscv64.*-kernel.*kernel.bin" 2>/dev/null

# Cleanup function
cleanup() {
    rm -f "$TMP_GDB_SCRIPT"
    if kill -0 $QEMU_PID 2>/dev/null; then
        echo "Killing QEMU..."
        kill $QEMU_PID 2>/dev/null
    fi
}

# Set trap to cleanup on exit
trap cleanup EXIT

# Start QEMU in background with GDB server
echo "Starting QEMU in debug mode..."
qemu-system-riscv64 \
    -M virt \
    -cpu rv64 \
    -nographic \
    -smp 1 \
    -m 1G \
    -kernel "$KERNEL_BIN" \
    -S \
    -gdb tcp::1234 &

QEMU_PID=$!

# Give QEMU time to start
sleep 1

# Check if QEMU started successfully
if ! kill -0 $QEMU_PID 2>/dev/null; then
    echo "Error: Failed to start QEMU"
    exit 1
fi

echo "QEMU started with PID $QEMU_PID"
echo "Starting GDB..."

# Create a temporary GDB init script that loads symbols and user script
TMP_GDB_SCRIPT=$(mktemp /tmp/gdb-init.XXXXXX)
cat > "$TMP_GDB_SCRIPT" << EOF
# Load symbols from ELF file
file $KERNEL_ELF

# Connect to QEMU
target remote :1234

# Source the user's GDB script
source $GDB_SCRIPT
EOF

# Start GDB with our init script
riscv64-elf-gdb -x "$TMP_GDB_SCRIPT"

echo "Debug session ended"