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

.globl exception_trigger_breakpoint_holding_vector_registers
.type exception_trigger_breakpoint_holding_vector_registers, @function
exception_trigger_breakpoint_holding_vector_registers:
    movl 4(%esp), %eax    # the 128 bytes XMM0-XMM7 hold across INT3
    movups 0(%eax), %xmm0
    movups 16(%eax), %xmm1
    movups 32(%eax), %xmm2
    movups 48(%eax), %xmm3
    movups 64(%eax), %xmm4
    movups 80(%eax), %xmm5
    movups 96(%eax), %xmm6
    movups 112(%eax), %xmm7
    int3
    movl 8(%esp), %eax    # where XMM0-XMM7 go once INT3 returned
    movups %xmm0, 0(%eax)
    movups %xmm1, 16(%eax)
    movups %xmm2, 32(%eax)
    movups %xmm3, 48(%eax)
    movups %xmm4, 64(%eax)
    movups %xmm5, 80(%eax)
    movups %xmm6, 96(%eax)
    movups %xmm7, 112(%eax)
    ret

.globl exception_trigger_fill_vector_registers
.type exception_trigger_fill_vector_registers, @function
exception_trigger_fill_vector_registers:
    movl 4(%esp), %eax    # the 128 bytes XMM0-XMM7 take
    movups 0(%eax), %xmm0
    movups 16(%eax), %xmm1
    movups 32(%eax), %xmm2
    movups 48(%eax), %xmm3
    movups 64(%eax), %xmm4
    movups 80(%eax), %xmm5
    movups 96(%eax), %xmm6
    movups 112(%eax), %xmm7
    ret
