.section .rodata.ap_trampoline, "a", @progbits

.set AP_TRAMPOLINE_BASE_PHYS, 0x00008000
.set AP_MAILBOX_BASE_PHYS,    0x00007D00
.set AP_HIGH_HALF_BASE,       0xC0000000
.set AP_ACK_WORD_PHYS,        0x00007DF0
.set AP_C_ENTRY_WORD_PHYS,    0x00007DF2
.set AP_ACK_MAGIC,            0x00004150
.set AP_C_ENTRY_MAGIC,        0x00004350

.set AP_MAILBOX_APIC_ID,      (AP_MAILBOX_BASE_PHYS + 4)
.set AP_MAILBOX_CR3,          (AP_MAILBOX_BASE_PHYS + 8)
.set AP_MAILBOX_STACK_TOP,    (AP_MAILBOX_BASE_PHYS + 12)
.set AP_MAILBOX_C_ENTRY,      (AP_MAILBOX_BASE_PHYS + 16)
.set AP_MAILBOX_LOGICAL_SLOT, (AP_MAILBOX_BASE_PHYS + 20)
.set AP_MAILBOX_MAIN_LOOP,    (AP_MAILBOX_BASE_PHYS + 24)

.set AP_MAILBOX_APIC_ID_HIGH,      (AP_HIGH_HALF_BASE + AP_MAILBOX_APIC_ID)
.set AP_MAILBOX_STACK_TOP_HIGH,    (AP_HIGH_HALF_BASE + AP_MAILBOX_STACK_TOP)
.set AP_MAILBOX_C_ENTRY_HIGH,      (AP_HIGH_HALF_BASE + AP_MAILBOX_C_ENTRY)
.set AP_MAILBOX_LOGICAL_SLOT_HIGH, (AP_HIGH_HALF_BASE + AP_MAILBOX_LOGICAL_SLOT)
.set AP_MAILBOX_MAIN_LOOP_HIGH,    (AP_HIGH_HALF_BASE + AP_MAILBOX_MAIN_LOOP)
.set AP_C_ENTRY_WORD_HIGH,         (AP_HIGH_HALF_BASE + AP_C_ENTRY_WORD_PHYS)

.global ap_trampoline_blob_start
.global ap_trampoline_blob_end

.code16
ap_trampoline_blob_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp

    movw $AP_ACK_MAGIC, AP_ACK_WORD_PHYS
    movw $0x0000, AP_C_ENTRY_WORD_PHYS

    lgdt (AP_TRAMPOLINE_BASE_PHYS + (ap_trampoline_gdt_descriptor - ap_trampoline_blob_start))

    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $(AP_TRAMPOLINE_BASE_PHYS + (ap_trampoline_protected_entry - ap_trampoline_blob_start))

.code32
ap_trampoline_protected_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movl AP_MAILBOX_CR3, %eax
    movl %eax, %cr3

    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0
    movl $(AP_HIGH_HALF_BASE + AP_TRAMPOLINE_BASE_PHYS + (ap_trampoline_paged_entry - ap_trampoline_blob_start)), %eax
    jmp *%eax

ap_trampoline_paged_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movl AP_MAILBOX_STACK_TOP_HIGH, %esp

    movl AP_MAILBOX_C_ENTRY_HIGH, %eax
    movzbl AP_MAILBOX_APIC_ID_HIGH, %ebx
    movl AP_MAILBOX_LOGICAL_SLOT_HIGH, %ecx
    movl AP_MAILBOX_STACK_TOP_HIGH, %edx

    pushl %edx
    pushl %ecx
    pushl %ebx
    call *%eax
    addl $12, %esp

    movw $AP_C_ENTRY_MAGIC, AP_C_ENTRY_WORD_HIGH

    movl AP_MAILBOX_MAIN_LOOP_HIGH, %eax
    testl %eax, %eax
    jz ap_trampoline_halt
    call *%eax

ap_trampoline_halt:
    cli
ap_trampoline_halt_loop:
    hlt
    jmp ap_trampoline_halt_loop

.align 8
ap_trampoline_gdt:
    .long 0x00000000
    .long 0x00000000
    .long 0x0000FFFF
    .long 0x00CF9A00
    .long 0x0000FFFF
    .long 0x00CF9200

ap_trampoline_gdt_descriptor:
    .word (24 - 1)
    .long (AP_TRAMPOLINE_BASE_PHYS + (ap_trampoline_gdt - ap_trampoline_blob_start))

ap_trampoline_blob_end:
