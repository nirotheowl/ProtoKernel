This project is a 64-bit experimental kernel intended to be run on ARMv8 and RISC-V targets. 

## Prerequisites

### ARM64 Prerequisites 

Note: On some distributions of Linux, `aarch64-none-elf` (or equivalent) may not be available. It is
**NOT** guaranteed that the `aarch64-linux-gnu-*` toolchain is compatible, so you may need to compile
the toolchain yourself from https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads.

The `linux` versions are listed because as of writing this, they do work, but that could change. 

#### Ubuntu (22.04 or later)

```bash
sudo apt update
sudo apt install \
    gcc-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    qemu-system-arm \
    make \
    gdb-multiarch \
    git
```

#### Arch Linux

Install the required packages (note: some are AUR packages):

```bash
yay -S \
    qemu-full \
    make \
    git \
    arm-gnu-toolchain-aarch64-none-elf-bin
```

### RISC-V Prerequisites

The dependencies for RISC-V builds are different, namely the toolchain and QEMU packages. 

Note: Similarly to ARM's toolchain availability on some distributions, there is no guarantee that
the `riscv64-linux-gnu` version of the toolchains will be compatible with this kernel.

#### Ubuntu (22.04 or later) 

```bash
sudo apt update
sudo apt install \
    gcc-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu \
    qemu-system-misc \
    make \
    git
```

#### Arch Linux 

```bash
yay -S \
    riscv64-elf-gcc \
    riscv64-elf-binutils \
    qemu-full \
    make \
    git
``` 

## Building and Running

Source the environment script:
```bash
source env.sh
```

Building for ARM64: 
```bash 
build_arm64 
```

Building for RISC-V: 
```bash 
build_riscv
```

Running the ARM kernel:
```bash 
run_emu_arm64
```

Running the RISC-V kernel: 
```bash 
run_emu_riscv
```



## Documentation 

Details of kernel development, scripts for using the kernel and other relevant information can be 
found in the `docs/` folder. 

### Patch note for 2025-09-07
added a simple "hello" driver as a test and for future support.  

- Location: `kernel/drivers/hello/`  
- Header: `kernel/include/drivers/hello/hello.h`  
- Purpose: Demonstrates how to register a basic driver with the kernel subsystem  
- Usage: Automatically initialized in `kernel_main.c`  
- Notes: This driver prints a message or can be used as a template for future miscellaneous drivers.
(Made by nirotheowl)

*Last Updated: 2025-09-7*



