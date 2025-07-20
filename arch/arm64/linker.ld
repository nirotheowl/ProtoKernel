ENTRY(_start)

SECTIONS
{
    /* QEMU virt machine loads kernel at 0x40000000 by default */
    /* But U-Boot will load us at a different address, typically 0x40200000 */
    /* We'll use 0x40200000 as our base address */
    . = 0x40200000;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    . = ALIGN(4096);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
    }

    . = ALIGN(4096);
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    __bss_end = .;

    . = ALIGN(4096);
    . = . + 0x1000;     /* 4KB guard page */
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }
}

/* Define symbols for memory regions */
__kernel_start = 0x40200000;
__kernel_size = _kernel_end - __kernel_start;