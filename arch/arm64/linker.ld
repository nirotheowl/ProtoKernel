ENTRY(_start)

/* Get physical base from build system 
 * Since --defsym always defines the symbol (even as 0), we can't use DEFINED().
 * Instead, we always use the CONFIG_PHYS_RAM_BASE value if provided. */
KERNEL_PHYS_BASE = CONFIG_PHYS_RAM_BASE + 0x200000;

/* Higher-half kernel configuration */
KERNEL_VIRT_BASE = 0xFFFF000040200000;  /* Virtual address in higher half (fixed) */

/* Calculate the virtual-to-physical offset based on actual physical base */
VIRT_TO_PHYS_OFFSET = KERNEL_VIRT_BASE - KERNEL_PHYS_BASE;

SECTIONS
{
    . = KERNEL_VIRT_BASE;

    __text_start = .;
    .text : AT(ADDR(.text) - VIRT_TO_PHYS_OFFSET) {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    __text_end = .;

    . = ALIGN(4096);
    .rodata : AT(ADDR(.rodata) - VIRT_TO_PHYS_OFFSET) {
        *(.rodata .rodata.*)
        
        . = ALIGN(8);
        __irqchip_drivers_start = .;
        KEEP(*(.irqchip_drivers))
        __irqchip_drivers_end = .;
        
        . = ALIGN(8);
        __uart_drivers_start = .;
        KEEP(*(.uart_drivers))
        __uart_drivers_end = .;
    }

    . = ALIGN(4096);
    __data_start = .;
    .data : AT(ADDR(.data) - VIRT_TO_PHYS_OFFSET) {
        *(.data .data.*)
    }
    __data_end = .;

    . = ALIGN(4096);
    __bss_start = .;
    .bss : AT(ADDR(.bss) - VIRT_TO_PHYS_OFFSET) {
        *(.bss .bss.*)
        *(COMMON)
    }
    __bss_end = .;

    . = ALIGN(4096);
    . = . + 0x1000;     /* 4KB guard page */
    _kernel_end = .;
    
    /* Linux header image_size calculation */
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }
}

/* Memory region symbols */
/* Physical start is determined at runtime, not set here */
__kernel_virt_start = KERNEL_VIRT_BASE;
__kernel_size = _kernel_end - __kernel_virt_start;

/* Compatibility symbol */
__kernel_start = __kernel_virt_start;