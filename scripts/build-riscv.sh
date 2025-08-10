#!/bin/bash

# Build the kernel

echo "Building RISC-V kernel..."
make clean
make ARCH=riscv

if [ $? -eq 0 ]; then
    echo "Build successful!"
    ls -lh build/riscv/kernel.bin
else
    echo "Build failed!"
    exit 1
fi