# Toolchain
CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# Flags
CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -std=c11
CFLAGS += -march=armv8-a -mgeneral-regs-only
CFLAGS += -O2 -g
CFLAGS += -I kernel/include
# For higher-half kernel: use large code model to handle addresses > 4GB
CFLAGS += -mcmodel=large
# Disable PIC as it's incompatible with large model
CFLAGS += -fno-pic -fno-pie

LDFLAGS = -T arch/arm64/linker.ld -nostdlib
# Static linking required for large model
LDFLAGS += -static

# Directories
BUILD_DIR = build
KERNEL_DIR = kernel
ARCH_DIR = arch

# Output files
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
KERNEL_LST = $(BUILD_DIR)/kernel.lst

# Source files
KERNEL_SRCS = $(shell find $(KERNEL_DIR) -name '*.c')
ARCH_SRCS = $(shell find $(ARCH_DIR) -name '*.S')

# Object files
KERNEL_OBJS = $(KERNEL_SRCS:%.c=$(BUILD_DIR)/%.o)
ARCH_OBJS = $(ARCH_SRCS:%.S=$(BUILD_DIR)/%.o)
ALL_OBJS = $(KERNEL_OBJS) $(ARCH_OBJS)

# Default target
.PHONY: all
all: $(KERNEL_BIN) $(KERNEL_LST)

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

# Run targets
.PHONY: run
run: $(KERNEL_BIN)
	./scripts/run.sh

.PHONY: qemu
qemu: run

.PHONY: debug
debug: $(KERNEL_ELF)
	./scripts/run-debug.sh

.PHONY: display
display: $(KERNEL_BIN)
	./scripts/run-display.sh

# Help
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all      - Build kernel (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  run      - Run kernel in QEMU (headless)"
	@echo "  display  - Run kernel in QEMU with display"
	@echo "  debug    - Run kernel in QEMU with GDB"
	@echo "  help     - Show this help message"