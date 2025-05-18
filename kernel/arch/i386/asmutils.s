.global inb
	.type inb, @function
.global outb
	.type outb, @function

.section .text

# unsigned char inb(short port)
inb:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    xorl %eax, %eax
    inb %dx, %al
    popl %ebp
    ret

# void outb(short port, unsigned char toSend)
outb:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %edx
    movl 12(%ebp), %eax
    movb %al, %al
    outb %al, %dx
    popl %ebp
    ret
