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

    /*
    ** Every output section below must also collect its per-symbol variants
    ** (`.text.*`, `.rodata.*`, `.data.*`, `.bss.*`) and the COMDAT
    ** `.gnu.linkonce.*` forms. C++ emits one such section per inline function,
    ** per template instantiation and per function-local static, and libengine
    ** produces them by the thousand.
    **
    ** Leaving them out does NOT drop them: ld turns them into orphan sections
    ** and appends them wherever it sees fit, in practice past the last output
    ** section. Anything landing after `_kernel_end` is then reported as free
    ** RAM to the PMM, which hands it out, so the kernel ends up allocating
    ** memory it is already using for its own statics. The corruption surfaces
    ** far from its cause: a page table installed in one of those frames gets
    ** silently overwritten, a 4 MB window of the direct map disappears, and the
    ** next walk of an allocator free list faults inside kmalloc.
    **
    ** `_kernel_end` must cover the whole image. Keep these wildcards.
    */
    /*
    ** Start of the region that must never be written once the kernel is up:
    ** code (.text, .init, .fini), constants (.rodata, .eh_frame) and the
    ** constructor pointer tables (.ctors, .dtors), which are walked at boot and
    ** never again. `.data` opens the writable region, and because every output
    ** section here is ALIGN(4K) the boundary lands on a page by construction —
    ** which is what lets kernel_section_protection_apply() clear the R/W bit of
    ** a whole page range without splitting anything.
    **
    ** Keep `_kernel_read_only_end` immediately before `.data`. Placing a
    ** writable section inside the range would fault the kernel on its first
    ** store, and placing a read-only one after it would silently leave that
    ** section writable.
    */
    . = ALIGN(4K);
    _kernel_read_only_start = .;

    .text ALIGN(4K) : AT (ADDR (.text) - global_kernel_start)
    {
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
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
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
        /*
        ** `.eh_frame` is read-only unwind data. The kernel builds
        ** -fno-exceptions and never walks it, but gcc emits it anyway and ld
        ** was placing it as an orphan. It happened to land just after .rodata,
        ** inside `_kernel_end`, so the post-link check stayed green — an orphan
        ** that is correct by luck rather than by instruction. Collect it where
        ** it belongs.
        */
        *(.eh_frame)
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

    . = ALIGN(4K);
    _kernel_read_only_end = .;

    .data ALIGN(4K) : AT (ADDR (.data) - global_kernel_start)
    {
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
    }

    .bss ALIGN(4K) : AT (ADDR (.bss) - global_kernel_start)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(.bootstrap_stack)
    }

    _kernel_end = .;
}

PROVIDE(global_kernel_end = _kernel_end);
