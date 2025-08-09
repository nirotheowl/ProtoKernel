ENTRY(_start)

/* Higher-half kernel configuration */
KERNEL_VIRT_BASE = 0xFFFF000040200000;  /* Virtual address in higher half */
PHYS_OFFSET = 0x200000;                  /* 2MB offset where kernel starts relative to load base */

/* Calculate the virtual-to-physical offset */
VIRT_TO_PHYS_OFFSET = KERNEL_VIRT_BASE - PHYS_OFFSET;

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