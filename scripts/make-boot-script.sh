#!/bin/bash

# Compile U-Boot boot script
# This creates boot.scr which U-Boot will automatically execute

if ! command -v mkimage &> /dev/null; then
    echo "Error: mkimage not found. Please install u-boot-tools:"
    echo "  sudo apt-get install u-boot-tools"
    exit 1
fi

echo "Creating U-Boot boot script..."
mkimage -A arm64 -O linux -T script -C none -a 0 -e 0 -n "ProtoKernel boot script" -d scripts/boot.cmd build/boot.scr

if [ $? -eq 0 ]; then
    echo "Successfully created build/boot.scr"
else
    echo "Failed to create boot script"
    exit 1
fi