A 64-bit ARM kernel project for educational and experimental purposes. The name might change.

## Prerequisites

### Ubuntu (22.04 or later)

Install the required packages:

```bash
sudo apt update
sudo apt install \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    gdb-multiarch \
    qemu-system-arm \
    make
```

### Arch Linux

Install the required packages:

```bash
sudo pacman -S \
    aarch64-linux-gnu-gcc \
    aarch64-linux-gnu-gdb \
    qemu-full \
    make 
```

## Building and Running

1. Source the environment script:
```bash
source env.sh
```

This sets up the build environment and provides helper functions.

2. Build the kernel:
```bash
build
```

3. Run the kernel in QEMU:
```bash
run_emu
```
*Last Updated: 2025-08-01*
