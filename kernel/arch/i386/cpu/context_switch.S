/* cpu_context_switch and the first code of a prepared context (context.h). The
   save of each context picks its instructions, XSAVES first since it is the
   one a processor offering it runs; EDX:EAX at all ones asks for every
   component XCR0 enables (Intel SDM vol. 1, §13.6). */

#include <kernel/cpu/context.h>

.section .text

.globl cpu_context_switch
.type cpu_context_switch, @function
cpu_context_switch:
    movl 4(%esp), %ecx
    movl 8(%esp), %edx
    cmpl %ecx, %edx
    je 9f
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl %esp, CPU_CONTEXT_OFFSET_STACK_POINTER(%ecx)
    movl %edx, %ebx
    movl CPU_CONTEXT_OFFSET_EXTENDED_STATE(%ecx), %edi
    movl CPU_CONTEXT_OFFSET_SAVE(%ecx), %esi
    movl $0xFFFFFFFF, %eax
    movl %eax, %edx
    cmpl $EXTENDED_STATE_SAVE_XSAVES, %esi
    jne 1f
    xsaves (%edi)
    jmp 4f
1:
    cmpl $EXTENDED_STATE_SAVE_XSAVEOPT, %esi
    jne 2f
    xsaveopt (%edi)
    jmp 4f
2:
    cmpl $EXTENDED_STATE_SAVE_XSAVE, %esi
    jne 3f
    xsave (%edi)
    jmp 4f
3:
    fxsave (%edi)
4:
    movl CPU_CONTEXT_OFFSET_EXTENDED_STATE(%ebx), %edi
    movl CPU_CONTEXT_OFFSET_SAVE(%ebx), %esi
    cmpl $EXTENDED_STATE_SAVE_XSAVES, %esi
    jne 5f
    xrstors (%edi)
    jmp 7f
5:
    cmpl $EXTENDED_STATE_SAVE_FXSAVE, %esi
    je 6f
    xrstor (%edi)
    jmp 7f
6:
    fxrstor (%edi)
7:
    movl CPU_CONTEXT_OFFSET_STACK_POINTER(%ebx), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
9:
    ret

.globl cpu_context_start
.type cpu_context_start, @function
cpu_context_start:
    pushl %esi
    call *%ebx
8:
    cli
    hlt
    jmp 8b
