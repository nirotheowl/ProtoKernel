#!/bin/bash

# Environment setup for ARM64 kernel development
# Source this file: . ./env.sh

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Build the kernel
build() {
    echo "Building kernel..."
    ./scripts/build.sh
}

# Run emulator without U-Boot
run_emu() {
    echo "Running kernel in QEMU (direct boot)..."
    ./scripts/run.sh
}

# Run emulator with display
run_display() {
    echo "Running kernel in QEMU with display..."
    ./scripts/run-display.sh
}

# Run emulator with U-Boot
run_uboot() {
    echo "Running kernel in QEMU with U-Boot..."
    ./scripts/run-uboot.sh
}

# Run emulator in debug mode
run_debug() {
    echo "Running kernel in QEMU debug mode..."
    echo "Start GDB in another terminal with: ./scripts/debug-gdb.sh"
    ./scripts/run-debug.sh
}

# Clean build artifacts
clean() {
    echo "Cleaning build artifacts..."
    make clean
}

