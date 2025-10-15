.section .text
.global inb
.global outb

# unsigned char inb(short port)
.type inb, @function
inb:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    xorl %eax, %eax
    inb %dx, %al
    popl %ebp
    ret

# void outb(short port, unsigned char toSend)
.type outb, @function
outb:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    movl 12(%ebp), %eax
    movb %al, %al
    outb %al, %dx
    popl %ebp
    ret
