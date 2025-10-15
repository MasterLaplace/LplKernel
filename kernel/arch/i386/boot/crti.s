.section .init
.global _init
.type _init, @function
_init:
    push %ebp
    movl %esp, %ebp

/* x86_64
.section .init
.global _init
.type _init, @function
_init:
    push %rbp
    movl %rsp, %rbp
*/

.section .fini
.global _fini
.type _fini, @function
_fini:
    push %ebp
    movl %esp, %ebp

/* x86_64
.section .fini
.global _fini
.type _fini, @function
_fini:
    push %rbp
    movq %rsp, %rbp
*/
