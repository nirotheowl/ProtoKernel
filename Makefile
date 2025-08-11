# Architecture selection (default to ARM64)
ARCH ?= arm64

# Platform selection (default based on architecture)
ifeq ($(ARCH),riscv)
    PLATFORM ?= riscv_qemu
else
    PLATFORM ?= qemu_virt
endif

# Validate architecture
VALID_ARCHS := arm64 riscv
ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
    $(error Invalid ARCH=$(ARCH). Valid options: $(VALID_ARCHS))
endif

# Include platform configuration if it exists
-include configs/$(PLATFORM).conf

# Toolchain
ifeq ($(ARCH),arm64)
    CROSS_COMPILE ?= aarch64-none-elf-
else ifeq ($(ARCH),riscv)
    # Try to auto-detect RISC-V toolchain (different distros use different names)
    ifeq ($(CROSS_COMPILE),)
        ifneq ($(shell which riscv64-elf-gcc 2>/dev/null),)
            CROSS_COMPILE = riscv64-elf-
        else ifneq ($(shell which riscv64-unknown-elf-gcc 2>/dev/null),)
            CROSS_COMPILE = riscv64-unknown-elf-
        else ifneq ($(shell which riscv64-linux-gnu-gcc 2>/dev/null),)
            CROSS_COMPILE = riscv64-linux-gnu-
        else
            $(warning RISC-V toolchain not found. Please install or set CROSS_COMPILE)
            CROSS_COMPILE = riscv64-elf-
        endif
    endif
endif

CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# Common flags
CFLAGS_COMMON = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -std=c11
CFLAGS_COMMON += -O2 -g

# Include paths - now including architecture directories
CFLAGS_COMMON += -I kernel/include
CFLAGS_COMMON += -I arch/include
CFLAGS_COMMON += -I arch/$(ARCH)/include

# Architecture-specific flags
ifeq ($(ARCH),arm64)
    CFLAGS_ARCH = -march=armv8-a -mgeneral-regs-only
    CFLAGS_ARCH += -mcmodel=large -fno-pic -fno-pie
    # Pass platform configuration to C code
    ifdef CONFIG_PHYS_RAM_BASE
        CFLAGS_ARCH += -DCONFIG_PHYS_RAM_BASE=$(CONFIG_PHYS_RAM_BASE)
    endif
    LDSCRIPT = arch/$(ARCH)/linker.ld
    # Pass configuration to linker (default to QEMU virt if not set)
    CONFIG_PHYS_RAM_BASE ?= 0x40000000
    LDFLAGS_ARCH = --defsym=CONFIG_PHYS_RAM_BASE=$(CONFIG_PHYS_RAM_BASE)
else ifeq ($(ARCH),riscv)
    CFLAGS_ARCH = -march=rv64imac_zicsr_zifencei -mabi=lp64
    CFLAGS_ARCH += -mcmodel=medany -fno-pic -fno-pie
    LDSCRIPT = arch/$(ARCH)/linker.ld
    # RISC-V QEMU virt loads at 0x80200000
    CONFIG_PHYS_RAM_BASE ?= 0x80000000
    LDFLAGS_ARCH = --defsym=CONFIG_PHYS_RAM_BASE=$(CONFIG_PHYS_RAM_BASE)
endif

CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_ARCH)
LDFLAGS = -T $(LDSCRIPT) -nostdlib -static $(LDFLAGS_ARCH)

# Directories
BUILD_DIR = build/$(ARCH)
KERNEL_DIR = kernel
ARCH_DIR = arch/$(ARCH)

# Output files
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
KERNEL_LST = $(BUILD_DIR)/kernel.lst

# Source files
# Architecture-independent kernel sources
KERNEL_C_SRCS = $(shell find $(KERNEL_DIR) -name '*.c')

# Architecture-specific sources
ARCH_C_SRCS = $(shell find $(ARCH_DIR) -name '*.c' 2>/dev/null || true)
ARCH_ASM_SRCS = $(shell find $(ARCH_DIR) -name '*.S' 2>/dev/null || true)

# Object files
KERNEL_OBJS = $(KERNEL_C_SRCS:%.c=$(BUILD_DIR)/%.o)
ARCH_C_OBJS = $(ARCH_C_SRCS:%.c=$(BUILD_DIR)/%.o)
ARCH_ASM_OBJS = $(ARCH_ASM_SRCS:%.S=$(BUILD_DIR)/%.o)
ALL_OBJS = $(ARCH_ASM_OBJS) $(ARCH_C_OBJS) $(KERNEL_OBJS)

# Default target
.PHONY: all
all: $(KERNEL_BIN) $(KERNEL_LST)
	@echo "Build complete for $(ARCH) architecture (platform: $(PLATFORM))"

# Kernel binary
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "Kernel binary created: $@"
	@echo "Size: $$(stat -c%s $@) bytes"

# Kernel listing
$(KERNEL_LST): $(KERNEL_ELF)
	$(OBJDUMP) -D $< > $@

# Kernel ELF
$(KERNEL_ELF): $(ALL_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "Kernel ELF created: $@"

# C source compilation
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly source compilation
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean all architectures
.PHONY: clean
clean:
	rm -rf build/

# Clean current architecture only
.PHONY: clean-arch
clean-arch:
	rm -rf $(BUILD_DIR)

# Run targets
.PHONY: run
run: $(KERNEL_BIN)
	./scripts/run-$(ARCH).sh

.PHONY: qemu
qemu: run

.PHONY: debug
debug: $(KERNEL_ELF)
	./scripts/run-debug-$(ARCH).sh

.PHONY: display
display: $(KERNEL_BIN)
	./scripts/run-display-$(ARCH).sh

# Help
.PHONY: help
help:
	@echo "Multi-Architecture Kernel Build System"
	@echo "======================================"
	@echo "Build targets:"
	@echo "  make [ARCH=arm64] [PLATFORM=qemu_virt] - Build kernel for specified architecture/platform"
	@echo "  make clean         - Remove all build artifacts"
	@echo "  make clean-arch    - Remove build artifacts for current ARCH only"
	@echo ""
	@echo "Run targets:"
	@echo "  make run           - Run kernel in QEMU (headless)"
	@echo "  make debug         - Run kernel in QEMU with GDB server"
	@echo "  make display       - Run kernel in QEMU with display"
	@echo ""
	@echo "Available architectures: $(VALID_ARCHS)"
	@echo "Current architecture: $(ARCH)"
	@echo "Current platform: $(PLATFORM)"
	@echo ""
	@echo "Available platforms:"
	@echo "  qemu_virt  - QEMU virt machine (RAM at 0x40000000)"
	@echo "  rpi4       - Raspberry Pi 4 (RAM at 0x00000000)"
	@echo "  server     - ARM server (RAM at 0x80000000)"
	@echo "  riscv_qemu - RISC-V QEMU (RAM at 0x80000000)"