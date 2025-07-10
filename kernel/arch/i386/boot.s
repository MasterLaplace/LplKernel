.arch i386
.code32

# Declare constants for the multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set VIDEO,    1<<2             # video mode info (for future graphics)
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot
.set KERNEL_START, 0xC0000000   # kernel start address

# MODE CONFIGURATION - Set via build system
# GRAPHICS_MODE is passed as -DGRAPHICS_MODE=0 or -DGRAPHICS_MODE=1
.set GRAPHICS_MODE, 0

.if GRAPHICS_MODE
    .set FLAGS, ALIGN | MEMINFO | VIDEO     # Graphics mode enabled
    .set SCREEN_MODE, 0x1                   # Force graphics mode
    .set SCREEN_WIDTH, 1024                 # Target resolution width
    .set SCREEN_HEIGHT, 768                 # Target resolution height
    .set SCREEN_DEPTH, 32                   # 32-bit color depth
.else
    .set FLAGS, ALIGN | MEMINFO             # Text mode only
    .set SCREEN_MODE, 0x0                   # Text mode (ignored)
.endif

# Multiboot header - conditional graphics support
.section .multiboot.data, "aw"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.if GRAPHICS_MODE
    .long 0, 0, 0, 0, 0            # address header (not used)
    .long SCREEN_MODE              # mode_type (1 = force graphics)
    .long SCREEN_WIDTH
    .long SCREEN_HEIGHT
    .long SCREEN_DEPTH
.endif

.section .bootstrap_stack, "aw", @nobits
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

.section .bss, "aw", @nobits
    .align 4096
boot_page_directory:
    .skip 4096
boot_page_table1:
    .skip 4096

.global g_multiboot_info
g_multiboot_info:
    .skip 4

.section .multiboot.text, "a"
.global _start
    .type _start, @function
_start:
    # Save multiboot info pointer for later
    movl %ebx, %edx

    # Setup page table pointer and counters
    movl $(boot_page_table1 - KERNEL_START), %edi
    movl $0, %esi
    movl $1024, %ecx

page_mapping_loop:
    # Map first 1MB (0x00000000-0x00100000) for multiboot info and BIOS data
    cmpl $0x100000, %esi
    jl map_this_page

    # Map kernel area (_kernel_start to _kernel_end)
    cmpl $_kernel_start, %esi
    jl skip_page
    cmpl $(_kernel_end - KERNEL_START), %esi
    jge skip_page

map_this_page:
    movl %esi, %eax
    orl $0x003, %eax        # Present + Writable
    movl %eax, (%edi)

skip_page:
    addl $4096, %esi        # Next page
    addl $4, %edi           # Next page table entry
    loop page_mapping_loop

    # Map VGA buffer at the end of page table
    movl $(0x000B8000 | 0x003), boot_page_table1 - KERNEL_START + 1023 * 4
    # Setup page directory
    movl $(boot_page_table1 - KERNEL_START + 0x003), boot_page_directory - KERNEL_START + 0     # Identity mapping
    movl $(boot_page_table1 - KERNEL_START + 0x003), boot_page_directory - KERNEL_START + 768 * 4  # Higher half mapping

    # Enable paging
    movl $(boot_page_directory - KERNEL_START), %ecx
    movl %ecx, %cr3

    movl %cr0, %ecx
    orl $0x80010000, %ecx
    movl %ecx, %cr0

    # Jump to higher half
    lea higher_half_entry, %ecx
    jmp *%ecx

.section .text

higher_half_entry:
    # Unmap identity mapping (no longer needed)
    movl $0, boot_page_directory + 0

    # Flush TLB
    movl %cr3, %ecx
    movl %ecx, %cr3

    # Setup stack
    movl $stack_top, %esp
    movl $stack_top, %ebp

    # Convert multiboot info: physical addr + KERNEL_START = virtual addr
    movl %edx, %ebx
    addl $KERNEL_START, %ebx

    movl %ebx, g_multiboot_info

    call _init
    call kernel_main
    call _fini

    cli
halt_loop:
    hlt
    jmp halt_loop
