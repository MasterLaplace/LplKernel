.section .init
    popl %ebp
    ret

/* x86_64
.section .init
    popq %rbp
    ret
*/

.section .fini
    popl %ebp
    ret

/* x86_64
.section .fini
    popq %rbp
    ret
*/
