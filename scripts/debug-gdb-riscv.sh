#!/bin/bash

# Launch GDB in TUI mode to debug the kernel

KERNEL_ELF="build/riscv/kernel.elf"
GDB_PORT=1234

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: Kernel ELF not found at $KERNEL_ELF"
    echo "Please run 'make ARCH=riscv' first to build the kernel"
    exit 1
fi

# Check for RISC-V GDB (different distros use different names)
if command -v riscv64-elf-gdb &> /dev/null; then
    GDB=riscv64-elf-gdb
elif command -v riscv64-unknown-elf-gdb &> /dev/null; then
    GDB=riscv64-unknown-elf-gdb
elif command -v riscv64-linux-gnu-gdb &> /dev/null; then
    GDB=riscv64-linux-gnu-gdb
elif command -v gdb-multiarch &> /dev/null; then
    GDB=gdb-multiarch
else
    echo "Error: RISC-V GDB not found!"
    echo "Please install one of:"
    echo "  - riscv64-elf-gdb (Arch Linux)"
    echo "  - riscv64-unknown-elf-gdb (some distros)"
    echo "  - riscv64-linux-gnu-gdb (Debian/Ubuntu)"
    echo "  - gdb-multiarch (Debian/Ubuntu alternative)"
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
# Set architecture
set architecture riscv:rv64

# Load symbols FIRST (before setting breakpoints)
file $KERNEL_ELF

# Connect to QEMU
target remote :$GDB_PORT

# Enable TUI mode
tui enable

# Set useful breakpoints (now that symbols are loaded)
break init_riscv
break _start

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