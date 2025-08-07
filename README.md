This project is a 64-bit experimental kernel intended to be run on ARMv8 and RISC-V targets. 

## Prerequisites

Note: On some distributions of Linux, `aarch64-none-elf` (or equivalent) may not be available. It is
**NOT** guaranteed that the `aarch64-linux-gnu-*` toolchain is compatible, so you may need to compile
the toolchain yourself from https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads.

### Ubuntu (22.04 or later)

Install the required packages (note: ARM64 toolchain not available, you have to compile that
manually):

```bash
sudo apt update
sudo apt install \
    qemu-system-arm \
    make \
    gdb-multiarch \
    git
```

### Arch Linux

Install the required packages (note: some are AUR packages):

```bash
yay -S \
    qemu-full \
    make \
    git \
    arm-gnu-toolchain-aarch64-none-elf-bin
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

*Last Updated: 2025-08-06*
