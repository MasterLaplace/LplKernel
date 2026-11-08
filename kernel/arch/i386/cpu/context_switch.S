/* cpu_context_switch and the first code of a prepared context (context.h). */

#include <kernel/cpu/context.h>

.section .text

.globl cpu_context_switch
.type cpu_context_switch, @function
cpu_context_switch:
    movl 4(%esp), %ecx
    movl 8(%esp), %edx
    cmpl %ecx, %edx
    je 1f
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl %esp, CPU_CONTEXT_OFFSET_STACK_POINTER(%ecx)
    movl CPU_CONTEXT_OFFSET_STACK_POINTER(%edx), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
1:
    ret

.globl cpu_context_start
.type cpu_context_start, @function
cpu_context_start:
    pushl %esi
    call *%ebx
2:
    cli
    hlt
    jmp 2b
