#!/bin/bash

# Environment setup for multi-architecture kernel development
# Source this file: . ./env.sh

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Default architecture
DEFAULT_ARCH="arm64"

# Build the kernel for a specific architecture
build() {
    local arch=${1:-$DEFAULT_ARCH}
    echo "Building kernel for $arch..."
    make ARCH=$arch
}

build_arm64() {
    build arm64
}

build_riscv() {
    build riscv
}

# ARM64-specific functions
run_emu_arm64() {
    echo "Running ARM64 kernel in QEMU (direct boot)..."
    ./scripts/run-arm64.sh
}

run_display_arm64() {
    echo "Running ARM64 kernel in QEMU with display..."
    ./scripts/run-display-arm64.sh
}

run_debug_arm64() {
    echo "Running ARM64 kernel in QEMU debug mode..."
    echo "Start GDB in another terminal with: ./scripts/debug-gdb.sh"
    ./scripts/run-debug-arm64.sh
}

run_uboot_arm64() {
    echo "Running ARM64 kernel in QEMU with U-Boot..."
    ./scripts/run-uboot.sh
}

# RISC-V-specific functions (placeholders for now)
run_emu_riscv() {
    echo "Running RISC-V kernel in QEMU (direct boot)..."
    if [ -f "./scripts/run-riscv.sh" ]; then
        ./scripts/run-riscv.sh
    else
        echo "Error: RISC-V run script not yet implemented"
        return 1
    fi
}

run_display_riscv() {
    echo "Running RISC-V kernel in QEMU with display..."
    if [ -f "./scripts/run-display-riscv.sh" ]; then
        ./scripts/run-display-riscv.sh
    else
        echo "Error: RISC-V display script not yet implemented"
        return 1
    fi
}

run_debug_riscv() {
    echo "Running RISC-V kernel in QEMU debug mode..."
    if [ -f "./scripts/run-debug-riscv.sh" ]; then
        ./scripts/run-debug-riscv.sh
    else
        echo "Error: RISC-V debug script not yet implemented"
        return 1
    fi
}

# Legacy compatibility aliases (will print deprecation warning)
run_emu() {
    echo "Warning: run_emu is deprecated. Use run_emu_arm64 or run_emu_riscv instead."
    run_emu_arm64
}

run_display() {
    echo "Warning: run_display is deprecated. Use run_display_arm64 or run_display_riscv instead."
    run_display_arm64
}

run_debug() {
    echo "Warning: run_debug is deprecated. Use run_debug_arm64 or run_debug_riscv instead."
    run_debug_arm64
}

run_uboot() {
    echo "Warning: run_uboot is deprecated. Use run_uboot_arm64 instead."
    run_uboot_arm64
}

# Clean build artifacts
clean() {
    local arch=${1:-""}
    if [ -z "$arch" ]; then
        echo "Cleaning all build artifacts..."
        make cleanall
    else
        echo "Cleaning $arch build artifacts..."
        make ARCH=$arch clean
    fi
}

clean_arm64() {
    clean arm64
}

clean_riscv() {
    clean riscv
}

# Helper function to show available commands
kernel_help() {
    echo "Multi-Architecture Kernel Development Environment"
    echo "================================================="
    echo ""
    echo "Build commands:"
    echo "  build [arch]     - Build kernel for specified architecture (default: arm64)"
    echo "  build_arm64      - Build ARM64 kernel"
    echo "  build_riscv      - Build RISC-V kernel"
    echo ""
    echo "ARM64 commands:"
    echo "  run_emu_arm64    - Run ARM64 kernel in QEMU"
    echo "  run_display_arm64 - Run ARM64 kernel with display"
    echo "  run_debug_arm64  - Run ARM64 kernel in debug mode"
    echo "  run_uboot_arm64  - Run ARM64 kernel with U-Boot"
    echo ""
    echo "RISC-V commands:"
    echo "  run_emu_riscv    - Run RISC-V kernel in QEMU (not yet implemented)"
    echo "  run_display_riscv - Run RISC-V kernel with display (not yet implemented)"
    echo "  run_debug_riscv  - Run RISC-V kernel in debug mode (not yet implemented)"
    echo ""
    echo "Clean commands:"
    echo "  clean [arch]     - Clean build artifacts (all or specific arch)"
    echo "  clean_arm64      - Clean ARM64 build artifacts"
    echo "  clean_riscv      - Clean RISC-V build artifacts"
    echo ""
    echo "Legacy commands (deprecated):"
    echo "  run_emu, run_display, run_debug, run_uboot"
}

