/* cpu_context_switch, the first code of a prepared context, and the empty
   restore cpu_context_adopt runs (context.h). The save of each context picks
   its instructions, XSAVES first since it is the one a processor offering it
   runs. EDX:EAX at all ones asks for every component: XCR0's for XSAVE and
   XRSTOR, XCR0 | IA32_XSS for XSAVES and XRSTORS (Intel SDM vol. 1,
   253665-093US, §13.7 and §13.11). At zero, XRSTOR and XRSTORS load nothing
   and track every component as modified (§13.8.3). */

#include <kernel/cpu/context.h>

.section .text

.globl cpu_context_switch
.type cpu_context_switch, @function
cpu_context_switch:
    movl 4(%esp), %ecx
    movl 8(%esp), %edx
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
    ret

.globl cpu_context_start
.type cpu_context_start, @function
cpu_context_start:
    pushl %esi
    call *%ebx
    call cpu_context_report_returned_entry
    cli
8:
    hlt
    jmp 8b

.globl cpu_context_track_every_component_as_modified
.type cpu_context_track_every_component_as_modified, @function
cpu_context_track_every_component_as_modified:
    movl 4(%esp), %ecx
    movl CPU_CONTEXT_OFFSET_SAVE(%ecx), %eax
    cmpl $EXTENDED_STATE_SAVE_FXSAVE, %eax
    je 2f
    movl CPU_CONTEXT_OFFSET_EXTENDED_STATE(%ecx), %ecx
    cmpl $EXTENDED_STATE_SAVE_XSAVES, %eax
    movl $0, %eax
    movl $0, %edx
    jne 1f
    xrstors (%ecx)
    ret
1:
    xrstor (%ecx)
2:
    ret
