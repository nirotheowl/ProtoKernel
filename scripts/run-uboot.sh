#!/bin/bash

# Run kernel with U-Boot in QEMU

KERNEL_BIN="build/kernel.bin"
UBOOT_BIN="reference/u-boot/u-boot.bin"

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
    echo "4. make CROSS_COMPILE=aarch64-none-elf-"
    echo "5. cp u-boot.bin ../"
    exit 1
fi

echo "Starting QEMU with U-Boot..."
echo "Press Ctrl-A X to exit QEMU"
echo ""
echo "U-Boot commands to boot the kernel:"
echo "  1. List files:     ls virtio 0"
echo "  2. Load kernel:    load virtio 0 0x40200000 kernel.bin"
echo "  3. Boot kernel:    booti 0x40200000 - \${fdtcontroladdr}"
echo ""
echo "Or use the one-liner:"
echo "  load virtio 0 0x40200000 kernel.bin && booti 0x40200000 - \${fdtcontroladdr}"

# Create boot script if it doesn't exist
if [ ! -f "build/boot.scr" ]; then
    echo "Creating boot script..."
    ./scripts/make-boot-script.sh
fi

# Create a temporary directory for the virtual disk
TMPDIR=$(mktemp -d)
cp "$KERNEL_BIN" "$TMPDIR/"
# Also copy boot script if it exists
if [ -f "build/boot.scr" ]; then
    cp "build/boot.scr" "$TMPDIR/"
    echo "Note: boot.scr copied - U-Boot will auto-boot the kernel"
fi

qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -m 1G \
    -nographic \
    -bios "$UBOOT_BIN" \
    -drive file=fat:rw:"$TMPDIR",format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0

# Clean up
rm -rf "$TMPDIR"