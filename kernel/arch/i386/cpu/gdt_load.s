.section .text
.global gdt_load
.global gdt_flush

.include "arch/i386/cpu/segment_selectors.inc"

/**
 * @brief Load the GDT using LGDT instruction
 *
 * C prototype: void gdt_load(const GlobalDescriptorTablePointer_t *gdtr);
 *
 * @param gdtr Pointer to GDTR structure (limit + base)
 */
.type gdt_load, @function
gdt_load:
    movl 4(%esp), %eax      # Get pointer to GDTR from stack
    lgdt (%eax)             # Load GDT
    ret

/**
 * @brief Flush (reload) segment registers after loading GDT
 *
 * C prototype: void gdt_flush(void);
 *
 * This reloads CS via a far jump and reloads DS/ES/FS/GS/SS with the kernel data selector.
 * The kernel code selector is 0x08 and kernel data selector is 0x10.
 */
.type gdt_flush, @function
gdt_flush:
    # Reload CS with kernel code selector via far jump
    ljmp $KERNEL_CS_SELECTOR, $.flush_cs

.flush_cs:
    # Reload all data segment registers with kernel data selector
    movw $KERNEL_DS_SELECTOR, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ret
