/* boot.S - Multiboot kernel entry point */
/* Note: This file uses .S extension to enable C preprocessor */

/* Declare constants for the multiboot header. */
.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set VIDEO,    1<<2             /* video mode info */

/* MODE CONFIGURATION - Set via build system */
/* GRAPHICS_MODE is passed as -DGRAPHICS_MODE=0 or -DGRAPHICS_MODE=1 */
#ifndef GRAPHICS_MODE
#define GRAPHICS_MODE 0
#endif

#if GRAPHICS_MODE == 1
    .set FLAGS, ALIGN | MEMINFO | VIDEO     /* Graphics mode enabled */
#else
    .set FLAGS, ALIGN | MEMINFO             /* Text mode only */
#endif

.set MAGIC,    0x1BADB002       /* 'magic number' lets bootloader find the header */
.set CHECKSUM, -(MAGIC + FLAGS) /* checksum of above, to prove we are multiboot */
.set KERNEL_START, 0xC0000000   /* kernel start address */

/* Graphics settings */
.set SCREEN_WIDTH, 1024         /* Target resolution width */
.set SCREEN_HEIGHT, 768         /* Target resolution height */
.set SCREEN_DEPTH, 32           /* 32-bit color depth */

/* Multiboot header */
/* Layout (per spec): magic, flags, checksum, [addr fields if bit16], [graphics fields if bit2] */
/* Since we use ELF (no AOUT_KLUDGE), we don't need address fields (bit 16 not set) */
/* Graphics fields are at offsets 32-44 when bit 2 is set */
.section .multiboot.data, "aw"
.align 4
multiboot_header:
    .long MAGIC                     /* offset 0: magic */
    .long FLAGS                     /* offset 4: flags */
    .long CHECKSUM                  /* offset 8: checksum */
#if GRAPHICS_MODE == 1
    /* Address fields (offsets 12-28) - set to 0 since we use ELF format */
    .long 0                         /* offset 12: header_addr (unused for ELF) */
    .long 0                         /* offset 16: load_addr (unused for ELF) */
    .long 0                         /* offset 20: load_end_addr (unused for ELF) */
    .long 0                         /* offset 24: bss_end_addr (unused for ELF) */
    .long 0                         /* offset 28: entry_addr (unused for ELF) */
    /* Graphics fields (offsets 32-44) - required when VIDEO flag (bit 2) is set */
    .long 0                         /* offset 32: mode_type (0 = linear graphics, 1 = EGA text) */
    .long SCREEN_WIDTH              /* offset 36: width (pixels) */
    .long SCREEN_HEIGHT             /* offset 40: height (pixels) */
    .long SCREEN_DEPTH              /* offset 44: depth (bits per pixel) */
#endif

.section .bootstrap_stack, "aw", @nobits
stack_bottom:
.skip 16384 /* 16 KiB */
stack_top:

.section .bss, "aw", @nobits
    .align 4096
.global boot_page_directory
boot_page_directory:
    .skip 4096
.global boot_page_tables
boot_page_tables:
    .skip 4096 * 16

/* Variable globale pour stocker le multiboot info */
.global multiboot_info
multiboot_info:
    .skip 4

.section .multiboot.text, "a"
.global _start
    .type _start, @function
_start:
    /* Save multiboot info pointer for later */
    movl %ebx, %edx

    /* Setup page table pointer and counters */
    movl $(boot_page_tables - KERNEL_START), %edi
    movl $0, %esi
    movl $16384, %ecx

page_mapping_loop:
    /* Map ALL pages 0–16MB unconditionally (Present + Writable).
     * We need every physical address reachable via phys_to_virt() so that
     * the PMM can push pages onto the free-list by writing through their
     * virtual alias.  The PMM itself decides which pages are actually free;
     * the page tables just need to have valid entries for the whole range. */
    movl %esi, %eax
    orl $0x003, %eax        /* Present + Writable */
    movl %eax, (%edi)

    addl $4096, %esi        /* Next page */
    addl $4, %edi           /* Next page table entry */
    loop page_mapping_loop

    /* Setup page directory */
    /* Identity mapping : PD[0] → boot_page_tables[0] (phys 0x0–0x3FFFFF) */
    movl $(boot_page_tables - KERNEL_START + 0x003), boot_page_directory - KERNEL_START + 0 * 4
    /* Higher-half mapping : PD[768..783] → boot_page_tables[0..15] (virt 0xC0000000–0xC3FFFFFF) */
    .set i, 0
    .rept 16
        movl $(boot_page_tables - KERNEL_START + 4096 * i + 0x003), boot_page_directory - KERNEL_START + (768 + i) * 4
        .set i, i + 1
    .endr

    /* Enable paging */
    movl $(boot_page_directory - KERNEL_START), %ecx
    movl %ecx, %cr3

    movl %cr0, %ecx
    orl $0x80010000, %ecx
    movl %ecx, %cr0

    /* Jump to higher half */
    lea higher_half_entry, %ecx
    jmp *%ecx

.section .text

higher_half_entry:
    /* Unmap identity mapping (no longer needed) */
    movl $0, boot_page_directory + 0

    /* Flush TLB */
    movl %cr3, %ecx
    movl %ecx, %cr3

    /* Setup stack */
    movl $stack_top, %esp
    movl $stack_top, %ebp

    /* Convert multiboot info: physical addr + KERNEL_START = virtual addr */
    movl %edx, %ebx
    addl $KERNEL_START, %ebx

    /* Store multiboot info in global variable for constructor access */
    movl %ebx, multiboot_info

    /* Also push it for compatibility (though kernel_main won't use it now) */
    pushl %ebx

    /* Call constructors, kernel, destructors */
    call _init
    call kernel_main
    addl $4, %esp           /* Clean stack */
    call _fini

    /* Halt */
    cli
halt_loop:
    hlt
    jmp halt_loop
