#!/bin/bash

# Test server platform kernel at 0x80200000

KERNEL_BIN="build/arm64/kernel.bin"
UBOOT_BIN="reference/u-boot/u-boot.bin"

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel not found. Run 'make PLATFORM=server' first."
    exit 1
fi

if [ ! -f "$UBOOT_BIN" ]; then
    echo "Error: U-Boot not found at $UBOOT_BIN"
    exit 1
fi

# Create temp directory for virtual disk
TMPDIR=$(mktemp -d)
cp "$KERNEL_BIN" "$TMPDIR/"

# Create boot script that loads at 0x80200000
cat > "$TMPDIR/boot.cmd" << 'EOF'
echo "Loading kernel at 0x80200000 for server platform..."
load virtio 0 0x80200000 kernel.bin
echo "Kernel loaded, booting..."
booti 0x80200000 - ${fdtcontroladdr}
EOF

# Compile boot script if mkimage available
if command -v mkimage >/dev/null 2>&1; then
    mkimage -A arm64 -O linux -T script -C none -d "$TMPDIR/boot.cmd" "$TMPDIR/boot.scr" >/dev/null 2>&1
fi

echo "Testing server platform kernel at 0x80200000..."
echo "This should boot if the kernel was built with PLATFORM=server"
echo ""

# Run QEMU with 4GB RAM (server config)
timeout 10 qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -m 4G \
    -nographic \
    -bios "$UBOOT_BIN" \
    -drive file=fat:rw:"$TMPDIR",format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0

# Clean up
rm -rf "$TMPDIR"