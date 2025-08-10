#!/bin/bash

# Build the kernel

echo "Building ARM64 kernel..."
make clean
make ARCH=arm64

if [ $? -eq 0 ]; then
    echo "Build successful!"
    ls -lh build/arm64/kernel.bin
else
    echo "Build failed!"
    exit 1
fi