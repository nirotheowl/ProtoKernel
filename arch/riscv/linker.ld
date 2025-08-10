/*
 * arch/riscv/linker.ld
 * 
 * RISC-V kernel linker script
 * Memory layout for QEMU virt machine
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

/* QEMU RISC-V virt loads kernel at 0x80200000 by default */
KERNEL_PHYS_BASE = 0x80200000;

SECTIONS
{
    . = KERNEL_PHYS_BASE;
    
    _kernel_start = .;
    __kernel_start = .;
    
    /* Code sections */
    __text_start = .;
    .text : {
        *(.text.boot)
        *(.text*)
    }
    __text_end = .;
    
    . = ALIGN(4096);
    
    /* Read-only data */
    .rodata : {
        *(.rodata*)
    }
    
    . = ALIGN(4096);
    
    /* Data sections */
    __data_start = .;
    .data : {
        *(.data*)
    }
    __data_end = .;
    
    . = ALIGN(4096);
    
    /* BSS section */
    _bss_start = .;
    __bss_start = .;
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    _bss_end = .;
    __bss_end = .;
    
    . = ALIGN(4096);
    
    /* Stack - 64KB */
    _stack_bottom = .;
    . += 0x10000;
    _stack_top = .;
    
    _kernel_end = .;
}

/* Export symbols for use in code */
PROVIDE(_end = .);
PROVIDE(_kernel_phys_base = KERNEL_PHYS_BASE);
PROVIDE(kernel_phys_base = KERNEL_PHYS_BASE);