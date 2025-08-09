#!/bin/bash

# Test kernel at different load addresses using U-Boot
# Usage: ./run-testaddr.sh [address]
# Default: 0x41000000 (different from default 0x40200000)

ADDR=${1:-0x41000000}
KERNEL_BIN="build/arm64/kernel.bin"
UBOOT_BIN="reference/u-boot/u-boot.bin"

echo "========================================="
echo "Testing kernel at address: $ADDR"
echo "========================================="

# Check kernel exists
if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel not found. Run 'source env.sh && build' first."
    exit 1
fi

# Check U-Boot exists
if [ ! -f "$UBOOT_BIN" ]; then
    echo "Error: U-Boot not found at $UBOOT_BIN"
    echo "Build U-Boot with:"
    echo "  cd reference/u-boot"
    echo "  make qemu_arm64_defconfig"
    echo "  make CROSS_COMPILE=aarch64-none-elf-"
    exit 1
fi

# Create temp directory for virtual disk
TMPDIR=$(mktemp -d)
cp "$KERNEL_BIN" "$TMPDIR/"

# Create U-Boot script for this address
cat > "$TMPDIR/boot.cmd" << EOF
echo "========================================="
echo "Loading kernel at $ADDR"
echo "========================================="
load virtio 0 $ADDR kernel.bin
echo "Kernel loaded, booting..."
booti $ADDR - \${fdtcontroladdr}
EOF

# Compile boot script
if command -v mkimage >/dev/null 2>&1; then
    mkimage -A arm64 -O linux -T script -C none -d "$TMPDIR/boot.cmd" "$TMPDIR/boot.scr" >/dev/null 2>&1
    echo "Auto-boot script created"
else
    echo "mkimage not found - you'll need to manually type:"
    echo "  load virtio 0 $ADDR kernel.bin"
    echo "  booti $ADDR - \${fdtcontroladdr}"
fi

echo ""
echo "Starting QEMU with U-Boot..."
echo "Press Ctrl-A X to exit"
echo ""

# Run QEMU with U-Boot
qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -m 4G \
    -nographic \
    -bios "$UBOOT_BIN" \
    -drive file=fat:rw:"$TMPDIR",format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0

# Clean up
rm -rf "$TMPDIR"