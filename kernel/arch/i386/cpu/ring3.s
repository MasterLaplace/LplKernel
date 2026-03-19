.section .text

.include "arch/i386/cpu/segment_selectors.inc"

.global ring3_enter
.type ring3_enter, @function
# void ring3_enter(uint32_t user_eip, uint32_t user_esp)
ring3_enter:
    movl 4(%esp), %edx      # user_eip
    movl 8(%esp), %eax      # user_esp

    movw $USER_DS_SELECTOR, %cx
    movw %cx, %ds
    movw %cx, %es
    movw %cx, %fs
    movw %cx, %gs

    pushl $USER_DS_SELECTOR
    pushl %eax
    pushfl
    popl %eax
    orl $0x200, %eax        # IF=1
    pushl %eax
    pushl $USER_CS_SELECTOR
    pushl %edx
    iret
