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

    /*
    ** C++ global constructor / destructor support.
    **
    ** Two ABIs coexist in this tree:
    **   - i686-elf gcc 10.2.0 emits global constructors into the legacy
    **     `.ctors` section and relies on crtbegin/crtend bracketing the list
    **     plus the `_init`/`_fini` frames (crti + crtbegin body + crtn) to walk
    **     it via __do_global_ctors_aux.
    **   - Newer toolchains (and the ARM port) emit into `.init_array`, iterated
    **     directly by the kernel.
    **
    ** Collecting all of `.init`/`.fini`, `.init_array`/`.fini_array` and the
    ** legacy `.ctors`/`.dtors` here makes the kernel correct under both ABIs:
    ** kernel_run_global_constructors() calls `_init()` (drives `.ctors`) AND
    ** iterates `.init_array`; exactly one of the two is populated for any given
    ** compiler, so neither set of constructors runs twice.
    **
    ** These output sections must exist explicitly: without them the crti /
    ** crtbegin / crtn `.init` fragments are placed as unordered orphan sections
    ** and `_init` never executes the constructor walk.
    */
    .init ALIGN(4K) : AT (ADDR (.init) - global_kernel_start)
    {
        KEEP(*(.init))
    }

    .fini ALIGN(4K) : AT (ADDR (.fini) - global_kernel_start)
    {
        KEEP(*(.fini))
    }

    .rodata ALIGN(4K) : AT (ADDR (.rodata) - global_kernel_start)
    {
        *(.rodata)
    }

    .init_array ALIGN(4K) : AT (ADDR (.init_array) - global_kernel_start)
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    }

    .fini_array ALIGN(4K) : AT (ADDR (.fini_array) - global_kernel_start)
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }

    .ctors ALIGN(4K) : AT (ADDR (.ctors) - global_kernel_start)
    {
        /* crtbegin opens the list (__CTOR_LIST__), crtend closes it (0). */
        KEEP(*crtbegin.o(.ctors))
        KEEP(*crtbegin?.o(.ctors))
        KEEP(*(EXCLUDE_FILE(*crtend.o *crtend?.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
    }

    .dtors ALIGN(4K) : AT (ADDR (.dtors) - global_kernel_start)
    {
        KEEP(*crtbegin.o(.dtors))
        KEEP(*crtbegin?.o(.dtors))
        KEEP(*(EXCLUDE_FILE(*crtend.o *crtend?.o) .dtors))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
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

PROVIDE(global_kernel_end = _kernel_end);
