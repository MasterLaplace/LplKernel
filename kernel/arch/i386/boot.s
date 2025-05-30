# Declare constants for the multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot
.set KERNEL_START, 0xC0000000   # kernel start address
.set SCREEN_WIDTH, 80
.set SCREEN_HEIGHT, 25
.set SCREEN_DEPTH, 24
.set SCREEN_MODE, 0x0           # 0x0 for text mode, 0x1 for graphics mode

.section .multiboot.data, "aw"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0, 0, 0, 0, 0
.long SCREEN_MODE
.long SCREEN_WIDTH
.long SCREEN_HEIGHT
.long SCREEN_DEPTH

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

.section .multiboot.text, "a"
.global _start
    .type _start, @function
_start:
    movl $(boot_page_table1 - KERNEL_START), %edi
    movl $0, %esi
    movl $1023, %ecx

1:
    cmpl $_kernel_start, %esi
    jl 2f
    cmpl $(_kernel_end - KERNEL_START), %esi
    jge 3f

    movl %esi, %edx
    orl $0x003, %edx
    movl %edx, (%edi)

2:
    addl $4096, %esi
    addl $4, %edi
    loop 1b

3:
    movl $(0x000B8000 | 0x003), boot_page_table1 - KERNEL_START + 1023 * 4
    movl $(boot_page_table1 - KERNEL_START + 0x003), boot_page_directory - KERNEL_START
    movl $(boot_page_table1 - KERNEL_START + 0x003), boot_page_directory - KERNEL_START + 768 * 4

    movl $(boot_page_directory - KERNEL_START), %ecx
    movl %ecx, %cr3

    movl %cr0, %ecx
    orl $0x80010000, %ecx
    movl %ecx, %cr0

    lea 4f, %ecx
    jmp *%ecx
    ljmp  $0x08, $4f

.section .text

4:
    movl $0, boot_page_directory
    movl %cr3, %ecx
    movl %ecx, %cr3

    mov $stack_top, %esp
    mov $stack_top, %ebp

    push %ebx
    call _init
    call kernel_main
    call _fini

    cli
1:  hlt
    jmp 1b
