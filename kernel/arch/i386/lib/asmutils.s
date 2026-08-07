.section .text

# I/O Port Operations
.globl asmutils_input_byte
.type asmutils_input_byte, @function
asmutils_input_byte:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    xorl %eax, %eax
    inb %dx, %al
    popl %ebp
    ret

.globl asmutils_output_byte
.type asmutils_output_byte, @function
asmutils_output_byte:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    movl 12(%ebp), %eax
    movb %al, %al
    outb %al, %dx
    popl %ebp
    ret

.globl asmutils_input_dword
.type asmutils_input_dword, @function
asmutils_input_dword:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    inl %dx, %eax
    popl %ebp
    ret

.globl asmutils_output_dword
.type asmutils_output_dword, @function
asmutils_output_dword:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    movl 12(%ebp), %eax
    outl %eax, %dx
    popl %ebp
    ret

# Interrupt Control
.globl asmutils_enable_interrupts
.type asmutils_enable_interrupts, @function
asmutils_enable_interrupts:
    sti
    ret

.globl asmutils_disable_interrupts
.type asmutils_disable_interrupts, @function
asmutils_disable_interrupts:
    cli
    ret

# CPU Primitive Operations
.globl asmutils_halt
.type asmutils_halt, @function
asmutils_halt:
    hlt
    ret

.globl asmutils_no_operation
.type asmutils_no_operation, @function
asmutils_no_operation:
    nop
    ret

# CPU Register Access
.globl asmutils_get_current_stack_pointer
.type asmutils_get_current_stack_pointer, @function
asmutils_get_current_stack_pointer:
    movl %esp, %eax
    ret

.globl asmutils_invalidate_translation_lookaside_buffer
.type asmutils_invalidate_translation_lookaside_buffer, @function
asmutils_invalidate_translation_lookaside_buffer:
    movl %cr3, %eax
    movl %eax, %cr3
    ret

.globl asmutils_get_page_fault_linear_address
.type asmutils_get_page_fault_linear_address, @function
asmutils_get_page_fault_linear_address:
    movl %cr2, %eax
    ret

.globl asmutils_read_control_register_0
.type asmutils_read_control_register_0, @function
asmutils_read_control_register_0:
    movl %cr0, %eax
    ret

# CPU Information & Configuration
.globl asmutils_cpuid
.type asmutils_cpuid, @function
asmutils_cpuid:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    /* %esi is callee-saved in the System V i386 ABI and this routine uses it as a
       scratch pointer for the four output stores. It was not being preserved, so a
       caller with a loop variable in %esi — which the compiler is entitled to do —
       got it silently overwritten by whatever CPUID leaf was read. */
    pushl %esi

    movl 8(%ebp), %eax
    movl 12(%ebp), %ecx
    cpuid

    movl 16(%ebp), %esi
    testl %esi, %esi
    jz asmutils_cpuid_skip_eax
    movl %eax, (%esi)
asmutils_cpuid_skip_eax:
    movl 20(%ebp), %esi
    testl %esi, %esi
    jz asmutils_cpuid_skip_ebx
    movl %ebx, (%esi)
asmutils_cpuid_skip_ebx:
    movl 24(%ebp), %esi
    testl %esi, %esi
    jz asmutils_cpuid_skip_ecx
    movl %ecx, (%esi)
asmutils_cpuid_skip_ecx:
    movl 28(%ebp), %esi
    testl %esi, %esi
    jz asmutils_cpuid_skip_edx
    movl %edx, (%esi)
asmutils_cpuid_skip_edx:
    popl %esi
    popl %ebx
    popl %ebp
    ret

# Model-Specific Registers
.globl asmutils_read_model_specific_register
.type asmutils_read_model_specific_register, @function
asmutils_read_model_specific_register:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %ecx
    rdmsr
    popl %ebp
    ret

.globl asmutils_write_model_specific_register
.type asmutils_write_model_specific_register, @function
asmutils_write_model_specific_register:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %ecx
    movl 12(%ebp), %eax
    movl 16(%ebp), %edx
    wrmsr
    popl %ebp
    ret

/*
** Timestamp counter, full 64 bits.
**
** Two 32-bit copies of this already exist as static inlines in tlsf.c and
** frame_arena.c, both of which keep only the low word because a duration is all
** they measure. The power floor needs the whole counter: an idle node sleeps for
** seconds at a stretch, and at a gigahertz the low word wraps every four.
*/
.globl asmutils_read_timestamp_counter
.type asmutils_read_timestamp_counter, @function
asmutils_read_timestamp_counter:
    rdtsc
    ret

/*
** MONITOR — arm a watch on a cache line.
**
** The processor remembers the line the address falls in; a subsequent MWAIT sleeps
** until anything writes it. That is what makes it better than HLT for a node whose
** wake-up comes from a device's DMA rather than from an interrupt: no IRQ is needed
** and no interrupt latency is paid.
**
** void asmutils_monitor(const void *address, uint32_t extensions, uint32_t hints)
*/
.globl asmutils_monitor
.type asmutils_monitor, @function
asmutils_monitor:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %eax
    movl 12(%ebp), %ecx
    movl 16(%ebp), %edx
    monitor
    popl %ebp
    ret

/*
** MWAIT — sleep until the monitored line is written.
**
** EAX carries the target C-state as a hint, ECX the extensions. Bit 0 of the
** extensions makes an unmasked interrupt a break event too, which is what keeps a
** sleeping core answerable to a timer it also armed.
**
** void asmutils_monitor_wait(uint32_t hints, uint32_t extensions)
*/
.globl asmutils_monitor_wait
.type asmutils_monitor_wait, @function
asmutils_monitor_wait:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %eax
    movl 12(%ebp), %ecx
    mwait
    popl %ebp
    ret
