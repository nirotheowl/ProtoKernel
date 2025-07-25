ENTRY(_start)

/* Higher-half kernel configuration */
KERNEL_PHYS_BASE = 0x40200000;  /* Physical load address */
KERNEL_VIRT_BASE = 0xFFFF000040200000;  /* Virtual address in higher half */

SECTIONS
{
    /* AT() directive: load at physical address, run at virtual address */
    . = KERNEL_VIRT_BASE;

    __text_start = .;
    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE) {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    __text_end = .;

    . = ALIGN(4096);
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE) {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);
    __data_start = .;
    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE) {
        *(.data .data.*)
    }
    __data_end = .;

    . = ALIGN(4096);
    __bss_start = .;
    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE) {
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
__kernel_phys_start = KERNEL_PHYS_BASE;
__kernel_virt_start = KERNEL_VIRT_BASE;
__kernel_size = _kernel_end - __kernel_virt_start;

/* Compatibility symbol */
__kernel_start = __kernel_phys_start;