ENTRY(_start)

PROVIDE(global_kernel_start = 0xC0000000);

SECTIONS
{
    . = 0x00100000;

    _kernel_start = .;
    .multiboot.data : {
        *(.multiboot.data)
    }

    .multiboot.text : {
        *(.multiboot.text)
    }

    . += global_kernel_start;

    .text ALIGN(4K) : AT (ADDR (.text) - global_kernel_start)
    {
        *(.text)
    }

    .rodata ALIGN(4K) : AT (ADDR (.rodata) - global_kernel_start)
    {
        *(.rodata)
    }

    .data ALIGN(4K) : AT (ADDR (.data) - global_kernel_start)
    {
        *(.data)
    }

    .bss ALIGN(4K) : AT (ADDR (.bss) - global_kernel_start)
    {
        *(COMMON)
        *(.bss)
        *(.bootstrap_stack)
    }

    _kernel_end = .;
}
