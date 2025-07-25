# U-Boot boot script for micl-arm-os kernel
# This script will be compiled to boot.scr using mkimage
# Location: scripts/boot.cmd
# Output: build/boot.scr

echo "Loading micl-arm-os kernel..."

# Try different storage devices in order
if test -e virtio 0 kernel.bin; then
    echo "Found kernel on virtio device"
    load virtio 0 0x40200000 kernel.bin
elif test -e mmc 0 kernel.bin; then
    echo "Found kernel on MMC device"
    load mmc 0 0x40200000 kernel.bin
elif test -e usb 0 kernel.bin; then
    echo "Found kernel on USB device"
    load usb 0 0x40200000 kernel.bin
else
    echo "ERROR: kernel.bin not found on any device!"
    exit
fi

echo "Booting kernel at 0x40200000..."
booti 0x40200000 - ${fdtcontroladdr}