#!/bin/bash

# Debug kernel at different load addresses using U-Boot and GDB
# Usage: ./scripts/debug-uboot.sh [address] [gdb_script]
# Default: 0x80200000 (server configuration)

ADDR=${1:-0x80200000}
GDB_SCRIPT=${2:-}
KERNEL_BIN="build/arm64/kernel.bin"
KERNEL_ELF="build/arm64/kernel.elf"
UBOOT_BIN="reference/u-boot/u-boot.bin"

echo "========================================="
echo "Debug kernel at address: $ADDR"
echo "========================================="

# Check files exist
if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: Kernel not found. Run 'source env.sh && build' first."
    exit 1
fi

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: Kernel ELF not found."
    exit 1
fi

if [ ! -f "$UBOOT_BIN" ]; then
    echo "Error: U-Boot not found at $UBOOT_BIN"
    exit 1
fi

# Create temp directory for virtual disk
TMPDIR=$(mktemp -d)
cp "$KERNEL_BIN" "$TMPDIR/"

# Create U-Boot script that waits before booting
# This gives us time to attach GDB
cat > "$TMPDIR/boot.cmd" << EOF
echo "========================================="
echo "Debug mode - preparing to load kernel at $ADDR"
echo "========================================="
echo "Waiting 3 seconds for GDB to attach..."
sleep 3
echo "Loading kernel..."
load virtio 0 $ADDR kernel.bin
echo "Kernel loaded at $ADDR"
echo "Starting kernel with: booti $ADDR - \${fdtcontroladdr}"
booti $ADDR - \${fdtcontroladdr}
EOF

# Compile boot script if mkimage available
if command -v mkimage >/dev/null 2>&1; then
    mkimage -A arm64 -O linux -T script -C none -d "$TMPDIR/boot.cmd" "$TMPDIR/boot.scr" >/dev/null 2>&1
fi

# Calculate physical base offset
DEFAULT_PHYS=0x40200000
OFFSET=$(printf "0x%x" $((ADDR - DEFAULT_PHYS)))

echo "Load offset from default: $OFFSET"

# Create GDB init file
cat > /tmp/gdb-init-uboot.gdb << EOF
# Connect to QEMU
target remote :1234

# Load symbols at the offset
# Since kernel is position-independent, adjust symbol addresses
symbol-file
add-symbol-file $KERNEL_ELF 0xffff000040200000+$OFFSET

# Helper functions for address translation with offset
define v2p
    set \$vaddr = \$arg0
    if \$vaddr >= 0xFFFF000040200000 && \$vaddr < 0xFFFF000080000000
        set \$paddr = (\$vaddr - 0xFFFF000040200000) + $ADDR
        printf "VA 0x%016lx -> PA 0x%016lx\n", \$vaddr, \$paddr
    else
        printf "Address not in kernel virtual range\n"
    end
end

define p2v
    set \$paddr = \$arg0
    if \$paddr >= $ADDR && \$paddr < ($ADDR + 0x40000000)
        set \$vaddr = (\$paddr - $ADDR) + 0xFFFF000040200000
        printf "PA 0x%016lx -> VA 0x%016lx\n", \$paddr, \$vaddr
    else
        printf "Address not in expected physical range\n"
    end
end

# Set breakpoint at entry (adjusted for load address)
b *($ADDR + 0x40)

echo \n=== Debugging kernel loaded at $ADDR ===\n
echo Symbol addresses adjusted for offset $OFFSET\n
echo U-Boot will auto-boot in a few seconds...\n
echo \n
echo Commands:\n
echo   c - Continue execution\n
echo   si - Step instruction\n
echo   info registers - Show registers\n
echo   x/10i \$pc - Show next 10 instructions\n
echo   v2p <addr> - Convert virtual to physical\n
echo   p2v <addr> - Convert physical to virtual\n
echo \n

# User's GDB script if provided
EOF

# Append user's GDB script if provided
if [ -n "$GDB_SCRIPT" ] && [ -f "$GDB_SCRIPT" ]; then
    echo "# User script: $GDB_SCRIPT" >> /tmp/gdb-init-uboot.gdb
    cat "$GDB_SCRIPT" >> /tmp/gdb-init-uboot.gdb
fi

# Start QEMU in background with GDB server
echo "Starting QEMU with U-Boot in debug mode..."
qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -m 4G \
    -nographic \
    -bios "$UBOOT_BIN" \
    -drive file=fat:rw:"$TMPDIR",format=raw,if=none,id=drive0 \
    -device virtio-blk-device,drive=drive0 \
    -S -gdb tcp::1234 &

QEMU_PID=$!
echo "QEMU started with PID $QEMU_PID"

# Give QEMU time to start
sleep 1

# Start GDB
echo "Starting GDB..."
aarch64-none-elf-gdb -q -x /tmp/gdb-init-uboot.gdb

# Cleanup
echo "Cleaning up..."
kill $QEMU_PID 2>/dev/null
rm -rf "$TMPDIR"
rm -f /tmp/gdb-init-uboot.gdb

echo "Debug session ended"