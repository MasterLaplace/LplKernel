.section .text
.global inb
.global outb
.global cpu_enable_interrupts
.global cpu_disable_interrupts
.global cpu_halt
.global cpu_no_operation
.global cpu_get_current_stack_pointer
.global cpu_reload_page_directory
.global cpu_get_page_fault_linear_address

# unsigned char inb(short port)
.type inb, @function
inb:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    xorl %eax, %eax
    inb %dx, %al
    popl %ebp
    ret

# void outb(short port, unsigned char toSend)
.type outb, @function
outb:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    movl 12(%ebp), %eax
    movb %al, %al
    outb %al, %dx
    popl %ebp
    ret

# void cpu_enable_interrupts(void)
.type cpu_enable_interrupts, @function
cpu_enable_interrupts:
    sti
    ret

# void cpu_disable_interrupts(void)
.type cpu_disable_interrupts, @function
cpu_disable_interrupts:
    cli
    ret

# void cpu_halt(void)
.type cpu_halt, @function
cpu_halt:
    hlt
    ret

# void cpu_no_operation(void)
.type cpu_no_operation, @function
cpu_no_operation:
    nop
    ret

# uint32_t cpu_get_current_stack_pointer(void)
.type cpu_get_current_stack_pointer, @function
cpu_get_current_stack_pointer:
    movl %esp, %eax
    ret

# void cpu_reload_page_directory(void)
.type cpu_reload_page_directory, @function
cpu_reload_page_directory:
    movl %cr3, %eax
    movl %eax, %cr3
    ret

# uint32_t cpu_get_page_fault_linear_address(void)
.type cpu_get_page_fault_linear_address, @function
cpu_get_page_fault_linear_address:
    movl %cr2, %eax
    ret
