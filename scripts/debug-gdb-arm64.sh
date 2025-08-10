#!/bin/bash

# Launch GDB in TUI mode to debug the kernel

KERNEL_ELF="build/arm64/kernel.elf"
GDB_PORT=1234

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: Kernel ELF not found at $KERNEL_ELF"
    echo "Please run 'make' first to build the kernel"
    exit 1
fi

# Check if aarch64-none-elf-gdb exists
if command -v aarch64-none-elf-gdb &> /dev/null; then
    GDB=aarch64-none-elf-gdb
else
    echo "Error: aarch64-none-elf-gdb not found!"
    echo "Please install aarch64-none-elf-gdb"
    exit 1
fi

echo "Starting $GDB in TUI mode..."
echo "Commands to get started:"
echo "  (gdb) target remote :$GDB_PORT"
echo "  (gdb) continue"
echo ""

# Create a temporary GDB init file
GDBINIT=$(mktemp)
cat > "$GDBINIT" << EOF
# Connect to QEMU
target remote :$GDB_PORT

# Set architecture
set architecture aarch64

# Enable TUI mode
tui enable

# Set useful breakpoints
break kernel_main
break _start

# Load symbols
file $KERNEL_ELF

# Display registers and assembly
layout regs

echo \n
echo Connected to QEMU on port $GDB_PORT\n
echo Type 'continue' to start execution\n
echo \n
EOF

# Launch GDB with our init file
$GDB -x "$GDBINIT"

# Clean up
rm -f "$GDBINIT"
