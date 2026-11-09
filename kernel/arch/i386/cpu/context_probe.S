/* The tests' probes of the x87, SSE and AVX registers across
   cpu_context_switch: C code cannot hold a register across a call, so these
   load and store them around it, in ContextProbeState_t (context_probe.h). */

#include <kernel/cpu/context_probe.h>

.section .text

/* void context_probe_hold_across_switch(const ContextProbeState_t *held,
       ContextProbeState_t *after, CpuContext_t *from, const CpuContext_t *to,
       uint32_t wide) */
.globl context_probe_hold_across_switch
.type context_probe_hold_across_switch, @function
context_probe_hold_across_switch:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %eax
    fldcw CONTEXT_PROBE_OFFSET_CONTROL_WORD(%eax)
    ldmxcsr CONTEXT_PROBE_OFFSET_SIMD_CONTROL(%eax)
    cmpl $0, 24(%ebp)
    je 1f
    vmovdqu 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm0
    vmovdqu 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm1
    vmovdqu 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm2
    vmovdqu 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm3
    vmovdqu 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm4
    vmovdqu 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm5
    vmovdqu 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm6
    vmovdqu 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm7
    jmp 2f
1:
    movdqu 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm0
    movdqu 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm1
    movdqu 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm2
    movdqu 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm3
    movdqu 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm4
    movdqu 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm5
    movdqu 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm6
    movdqu 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm7
2:
    pushl 20(%ebp)
    pushl 16(%ebp)
    call cpu_context_switch
    addl $8, %esp
    movl 12(%ebp), %eax
    fnstcw CONTEXT_PROBE_OFFSET_CONTROL_WORD(%eax)
    stmxcsr CONTEXT_PROBE_OFFSET_SIMD_CONTROL(%eax)
    cmpl $0, 24(%ebp)
    je 3f
    vmovdqu %ymm0, 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm1, 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm2, 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm3, 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm4, 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm5, 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm6, 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm7, 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    jmp 4f
3:
    movdqu %xmm0, 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm1, 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm2, 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm3, 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm4, 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm5, 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm6, 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm7, 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
4:
    popl %ebp
    ret

/* void context_probe_load_and_switch(const ContextProbeState_t *state,
       CpuContext_t *from, const CpuContext_t *to, uint32_t wide) */
.globl context_probe_load_and_switch
.type context_probe_load_and_switch, @function
context_probe_load_and_switch:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %eax
    fldcw CONTEXT_PROBE_OFFSET_CONTROL_WORD(%eax)
    ldmxcsr CONTEXT_PROBE_OFFSET_SIMD_CONTROL(%eax)
    cmpl $0, 20(%ebp)
    je 1f
    vmovdqu 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm0
    vmovdqu 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm1
    vmovdqu 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm2
    vmovdqu 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm3
    vmovdqu 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm4
    vmovdqu 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm5
    vmovdqu 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm6
    vmovdqu 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm7
    jmp 2f
1:
    movdqu 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm0
    movdqu 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm1
    movdqu 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm2
    movdqu 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm3
    movdqu 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm4
    movdqu 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm5
    movdqu 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm6
    movdqu 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm7
2:
    pushl 16(%ebp)
    pushl 12(%ebp)
    call cpu_context_switch
    addl $8, %esp
    popl %ebp
    ret

/* void context_probe_read(ContextProbeState_t *state, uint32_t wide) */
.globl context_probe_read
.type context_probe_read, @function
context_probe_read:
    movl 4(%esp), %eax
    fnstcw CONTEXT_PROBE_OFFSET_CONTROL_WORD(%eax)
    stmxcsr CONTEXT_PROBE_OFFSET_SIMD_CONTROL(%eax)
    cmpl $0, 8(%esp)
    je 1f
    vmovdqu %ymm0, 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm1, 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm2, 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm3, 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm4, 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm5, 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm6, 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    vmovdqu %ymm7, 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    ret
1:
    movdqu %xmm0, 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm1, 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm2, 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm3, 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm4, 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm5, 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm6, 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    movdqu %xmm7, 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax)
    ret

/* void context_probe_load(const ContextProbeState_t *state, uint32_t wide) */
.globl context_probe_load
.type context_probe_load, @function
context_probe_load:
    movl 4(%esp), %eax
    fldcw CONTEXT_PROBE_OFFSET_CONTROL_WORD(%eax)
    ldmxcsr CONTEXT_PROBE_OFFSET_SIMD_CONTROL(%eax)
    cmpl $0, 8(%esp)
    je 1f
    vmovdqu 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm0
    vmovdqu 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm1
    vmovdqu 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm2
    vmovdqu 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm3
    vmovdqu 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm4
    vmovdqu 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm5
    vmovdqu 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm6
    vmovdqu 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %ymm7
    ret
1:
    movdqu 0+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm0
    movdqu 32+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm1
    movdqu 64+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm2
    movdqu 96+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm3
    movdqu 128+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm4
    movdqu 160+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm5
    movdqu 192+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm6
    movdqu 224+CONTEXT_PROBE_OFFSET_VECTORS(%eax), %xmm7
    ret
