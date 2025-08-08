# Architecture selection (default to ARM64)
ARCH ?= arm64

# Validate architecture
VALID_ARCHS := arm64
ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
    $(error Invalid ARCH=$(ARCH). Valid options: $(VALID_ARCHS))
endif

# Toolchain
ifeq ($(ARCH),arm64)
    CROSS_COMPILE ?= aarch64-none-elf-
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
    LDSCRIPT = arch/$(ARCH)/linker.ld
endif

CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_ARCH)
LDFLAGS = -T $(LDSCRIPT) -nostdlib -static

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
	@echo "Build complete for $(ARCH) architecture"

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

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

# Clean all architectures
.PHONY: cleanall
cleanall:
	rm -rf build/

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
	@echo "  make [ARCH=arm64]  - Build kernel for specified architecture (default: arm64)"
	@echo "  make clean         - Remove build artifacts for current ARCH"
	@echo "  make cleanall      - Remove all build artifacts"
	@echo ""
	@echo "Run targets:"
	@echo "  make run           - Run kernel in QEMU (headless)"
	@echo "  make debug         - Run kernel in QEMU with GDB server"
	@echo "  make display       - Run kernel in QEMU with display"
	@echo ""
	@echo "Available architectures: $(VALID_ARCHS)"
	@echo "Current architecture: $(ARCH)"