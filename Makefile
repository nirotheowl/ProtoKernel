CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -std=c11
CFLAGS += -march=armv8-a -mgeneral-regs-only
CFLAGS += -O2 -g
CFLAGS += -I include

LDFLAGS = -T arch/arm64/linker.ld -nostdlib

BUILD_DIR = build
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
KERNEL_LST = $(BUILD_DIR)/kernel.lst

C_SOURCES = kernel/kernel.c 
ASM_SOURCES = arch/arm64/boot.S

C_OBJECTS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS = $(patsubst %.S, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

all: $(KERNEL_BIN) $(KERNEL_LST)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "Kernel binary created: $@"
	@echo "Size: $$(stat -c%s $@) bytes"

$(KERNEL_LST): $(KERNEL_ELF)
	$(OBJDUMP) -D $< > $@

$(KERNEL_ELF): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "Kernel ELF created: $@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

run: $(KERNEL_BIN)
	@echo "To run with QEMU, execute: make qemu"
	@echo "To run with U-Boot, see scripts/run-uboot.sh"

qemu: $(KERNEL_BIN)
	./scripts/run.sh

.PHONY: all clean run qemu
