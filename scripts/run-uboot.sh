#!/bin/bash

# Run kernel with U-Boot in QEMU

KERNEL_BIN="build/kernel.bin"
UBOOT_BIN="u-boot.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel binary not found at $KERNEL_BIN"
    echo "Please run 'make' first to build the kernel"
    exit 1
fi

if [ ! -f "$UBOOT_BIN" ]; then
    echo "Error: U-Boot binary not found at $UBOOT_BIN"
    echo "You need to download or build U-Boot for qemu_arm64_defconfig"
    echo ""
    echo "To build U-Boot:"
    echo "1. git clone https://github.com/u-boot/u-boot.git"
    echo "2. cd u-boot"
    echo "3. make qemu_arm64_defconfig"
    echo "4. make CROSS_COMPILE=aarch64-linux-gnu-"
    echo "5. cp u-boot.bin ../"
    exit 1
fi

echo "Starting QEMU with U-Boot..."
echo "Press Ctrl-A X to exit QEMU"
echo ""
echo "U-Boot commands to boot the kernel:"
echo "  setenv loadaddr 0x40200000"
echo "  setenv bootargs 'console=ttyAMA0'"
echo "  load host 0:1 \${loadaddr} kernel.bin"
echo "  booti \${loadaddr} - \${fdtcontroladdr}"

# Create a temporary directory for the virtual disk
TMPDIR=$(mktemp -d)
cp "$KERNEL_BIN" "$TMPDIR/"

qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a53 \
    -m 1G \
    -nographic \
    -bios "$UBOOT_BIN" \
    -drive file=fat:rw:"$TMPDIR",format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0

# Clean up
rm -rf "$TMPDIR"