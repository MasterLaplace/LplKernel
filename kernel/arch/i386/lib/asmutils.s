.section .text

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

.globl asmutils_save_flags_and_disable_interrupts
.type asmutils_save_flags_and_disable_interrupts, @function
asmutils_save_flags_and_disable_interrupts:
    pushfl
    popl %eax
    cli
    ret

.globl asmutils_restore_flags
.type asmutils_restore_flags, @function
asmutils_restore_flags:
    pushl 4(%esp)
    popfl
    ret

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

.globl asmutils_pause
.type asmutils_pause, @function
asmutils_pause:
    pause
    ret

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

.globl asmutils_cpuid
.type asmutils_cpuid, @function
asmutils_cpuid:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
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

.globl asmutils_read_timestamp_counter
.type asmutils_read_timestamp_counter, @function
asmutils_read_timestamp_counter:
    rdtsc
    ret

.globl asmutils_read_timestamp_counter_low
.type asmutils_read_timestamp_counter_low, @function
asmutils_read_timestamp_counter_low:
    rdtsc
    ret

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

.globl asmutils_store_zero_with_resume_address
.type asmutils_store_zero_with_resume_address, @function
asmutils_store_zero_with_resume_address:
    movl 4(%esp), %edx
    movl 8(%esp), %eax
    movl $1f, (%eax)
    movb $0x00, (%edx)
1:
    ret
