#!/bin/bash

# Generate compile_commands.json for CCLS/clangd
# This script creates a compilation database by parsing the Makefile

ARCH=${1:-arm64}
BUILD_DIR="build/$ARCH"
OUTPUT_FILE="compile_commands.json"

echo "Generating compile_commands.json for ARCH=$ARCH..."

# Start JSON array
echo "[" > "$OUTPUT_FILE"

# Find all C source files
FIRST=true
find kernel arch/$ARCH -name "*.c" | while read -r file; do
    # Skip newline and comma for first entry
    if [ "$FIRST" = true ]; then
        FIRST=false
    else
        echo "," >> "$OUTPUT_FILE"
    fi
    
    # Determine compiler flags based on architecture
    if [ "$ARCH" = "arm64" ]; then
        FLAGS="-Wall -Wextra -ffreestanding -nostdlib -nostartfiles -std=c11 -O2 -g"
        FLAGS="$FLAGS -march=armv8-a -mgeneral-regs-only -mcmodel=large -fno-pic -fno-pie"
        FLAGS="$FLAGS -DCONFIG_PHYS_RAM_BASE=0x40000000"
        TARGET="aarch64-none-elf"
    elif [ "$ARCH" = "riscv" ]; then
        FLAGS="-Wall -Wextra -ffreestanding -nostdlib -nostartfiles -std=c11 -O2 -g"
        FLAGS="$FLAGS -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -fno-pic -fno-pie"
        FLAGS="$FLAGS -DCONFIG_PHYS_RAM_BASE=0x80000000"
        TARGET="riscv64-unknown-elf"
    fi
    
    FLAGS="$FLAGS -I kernel/include -I arch/include -I arch/$ARCH/include"
    
    # Create compilation entry
    cat >> "$OUTPUT_FILE" << EOF
    {
        "directory": "$(pwd)",
        "file": "$file",
        "command": "clang -target $TARGET $FLAGS -c $file -o $BUILD_DIR/$(basename ${file%.c}.o)"
    }
EOF
done

# Find all assembly source files
find kernel arch/$ARCH -name "*.S" | while read -r file; do
    echo "," >> "$OUTPUT_FILE"
    
    # Determine assembler flags based on architecture
    if [ "$ARCH" = "arm64" ]; then
        FLAGS="-march=armv8-a"
        TARGET="aarch64-none-elf"
    elif [ "$ARCH" = "riscv" ]; then
        FLAGS="-march=rv64imac_zicsr_zifencei -mabi=lp64"
        TARGET="riscv64-unknown-elf"
    fi
    
    FLAGS="$FLAGS -I kernel/include -I arch/include -I arch/$ARCH/include"
    
    # Create compilation entry for assembly
    cat >> "$OUTPUT_FILE" << EOF
    {
        "directory": "$(pwd)",
        "file": "$file",
        "command": "clang -target $TARGET $FLAGS -c $file -o $BUILD_DIR/$(basename ${file%.S}.o)"
    }
EOF
done

# Close JSON array
echo "" >> "$OUTPUT_FILE"
echo "]" >> "$OUTPUT_FILE"

echo "compile_commands.json generated successfully!"
echo "You can now use CCLS or clangd with this compilation database."