#!/bin/bash
# Debug version of server boot test

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
echo "Created temp directory: $TMPDIR"

cp "$KERNEL_BIN" "$TMPDIR/"
echo "Copied kernel to $TMPDIR/kernel.bin"

# Create boot script that loads at 0x80200000
cat > "$TMPDIR/boot.cmd" << 'EOF'
echo "==========================================="
echo "Custom boot script executing!"
echo "==========================================="
echo "Loading kernel at 0x80200000 for server platform..."
load virtio 0 0x80200000 kernel.bin
echo "Kernel loaded, booting..."
echo "FDT at ${fdtcontroladdr}"
booti 0x80200000 - ${fdtcontroladdr}
EOF
echo "Created boot.cmd"

# Compile boot script with mkimage
if command -v mkimage >/dev/null 2>&1; then
    mkimage -A arm64 -O linux -T script -C none -d "$TMPDIR/boot.cmd" "$TMPDIR/boot.scr"
    echo "Compiled boot.scr with mkimage"
    ls -la "$TMPDIR/"
else
    echo "WARNING: mkimage not found, boot script won't work!"
fi

echo ""
echo "Starting QEMU..."
echo "Press Ctrl-A X to exit"
echo ""

# Run QEMU
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