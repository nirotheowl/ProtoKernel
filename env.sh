#!/bin/bash

# Environment setup for ARM64 kernel development
# Source this file: . ./env.sh

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Export project root
export MICL_ARM_OS_ROOT="$SCRIPT_DIR"

# Add scripts directory to PATH
export PATH="$SCRIPT_DIR/scripts:$PATH"

# Build the kernel
build() {
    echo "Building kernel..."
    "$SCRIPT_DIR/scripts/build.sh"
}

# Run emulator without U-Boot
run_emu() {
    echo "Running kernel in QEMU (direct boot)..."
    "$SCRIPT_DIR/scripts/run.sh"
}

# Run emulator with display
run_display() {
    echo "Running kernel in QEMU with display..."
    "$SCRIPT_DIR/scripts/run-display.sh"
}

# Run emulator with U-Boot
run_uboot() {
    echo "Running kernel in QEMU with U-Boot..."
    "$SCRIPT_DIR/scripts/run-uboot.sh"
}

# Run emulator in debug mode
run_debug() {
    echo "Running kernel in QEMU debug mode..."
    echo "Start GDB in another terminal with: ./scripts/debug-gdb.sh"
    "$SCRIPT_DIR/scripts/run-debug.sh"
}

# Clean build artifacts
clean() {
    echo "Cleaning build artifacts..."
    make -C "$SCRIPT_DIR" clean
}

# Show available commands
help() {
    echo "Available commands:"
    echo "  build       - Build the kernel"
    echo "  run_emu     - Run kernel in QEMU (direct boot)"
    echo "  run_display - Run kernel in QEMU with display window"
    echo "  run_uboot   - Run kernel in QEMU with U-Boot"
    echo "  run_debug   - Run kernel in QEMU debug mode"
    echo "  clean       - Clean build artifacts"
    echo "  help        - Show this help message"
}

echo "ARM64 kernel development environment loaded!"
echo "Type 'help' to see available commands"