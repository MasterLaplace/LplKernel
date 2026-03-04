.section .text
.global idt_load

/**
 * @brief Load the IDT using LIDT instruction
 *
 * C prototype: void idt_load(const InterruptDescriptorTableRegister_t *idtr);
 *
 * @param idtr Pointer to IDT Descriptor structure (size + offset of IDT)
 */
.type idt_load, @function
idt_load:
    movl 4(%esp), %eax      # Get pointer to IDTR from stack
    lidt (%eax)             # Load IDT
    ret                     # NOTE: sti is intentionally omitted here.
                            #       Enable interrupts after PIC initialization.
