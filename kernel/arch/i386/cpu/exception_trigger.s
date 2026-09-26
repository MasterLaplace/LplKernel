.section .text

.globl exception_trigger_single_step
.type exception_trigger_single_step, @function
exception_trigger_single_step:
    pushfl
    orl $0x100, (%esp)      # TF=1
    popfl
    nop
    ret

.globl exception_trigger_breakpoint
.type exception_trigger_breakpoint, @function
exception_trigger_breakpoint:
    int3
    ret

.globl exception_trigger_invalid_opcode
.type exception_trigger_invalid_opcode, @function
exception_trigger_invalid_opcode:
    ud2
    ret

.globl exception_trigger_general_protection
.type exception_trigger_general_protection, @function
exception_trigger_general_protection:
    xorl %eax, %eax
    movw %ax, %ds
    movl (%eax), %eax
    ret

.globl exception_trigger_double_fault_vector
.type exception_trigger_double_fault_vector, @function
exception_trigger_double_fault_vector:
    int $0x08
    ret
