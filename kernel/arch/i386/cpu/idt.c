#include <kernel/cpu/gdt.h>
#include <kernel/cpu/idt.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <stddef.h>

#define IDT_EXCEPTION_VECTOR_COUNT 32u
#define IDT_IRQ_VECTOR_COUNT       16u
#define IDT_TOTAL_VECTOR_COUNT     256u

////////////////////////////////////////////////////////////
// External assembly functions (idt_load.s)
////////////////////////////////////////////////////////////

/**
 * @brief Load an IDT using the LIDT instruction.
 * @param idtr Pointer to an IDTR structure (size + offset).
 */
extern void idt_load(const InterruptDescriptorTableRegister_t *idtr);

////////////////////////////////////////////////////////////
// Private helpers functions of the IDT module
////////////////////////////////////////////////////////////

/**
 * @brief Encode a single flat 32-bit IDT entry.
 *
 * @param entry            Target entry to populate.
 * @param isr              Pointer to the interrupt service routine.
 * @param k_code_selector  GDT selector for the code segment (0x08 = kernel code).
 * @param flags            Raw type-attributes byte (e.g. IDT_KERNEL_INTERRUPT_GATE).
 */
static inline void interrupt_descriptor_table_encode_flat_entry(InterruptDescriptorTableFlatEntry_t *entry, void *isr,
                                                                uint16_t k_code_selector, uint8_t flags)
{
    if (!entry)
        return;

    entry->isr_low = (uint32_t) isr & 0xFFFFu;
    entry->selector = k_code_selector;
    entry->reserved = 0u;
    entry->type_attributes = *(InterruptDescriptorTableTypeAttributes_t *) &flags;
    entry->isr_high = (uint32_t) isr >> 16u;
}

__attribute__((unused)) static inline void
interrupt_descriptor_table_encode_long_entry(InterruptDescriptorTableLongModeEntry_t *entry, void *isr,
                                             uint16_t k_code_selector, uint8_t flags)
{
    if (!entry)
        return;

    entry->isr_low = (uint64_t) isr & 0xFFFFu;
    entry->selector = k_code_selector;
    entry->interrupt_stack_table = 0u;
    entry->type_attributes = *(InterruptDescriptorTableTypeAttributes_t *) &flags;
    entry->isr_mid = ((uint64_t) isr >> 16u) & 0xFFFFu;
    entry->isr_high = ((uint64_t) isr >> 32u) & 0xFFFFFFFFu;
    entry->reserved = 0u;
}

////////////////////////////////////////////////////////////
// ISR stub pointer table (32 CPU exception vectors)
////////////////////////////////////////////////////////////

typedef void (*isr_fn_t)(void);

static const isr_fn_t idt_exception_stubs[IDT_EXCEPTION_VECTOR_COUNT] = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,  isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

static const isr_fn_t idt_irq_stubs[IDT_IRQ_VECTOR_COUNT] = {
    isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39, isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47,
};

static void interrupt_descriptor_table_clear(InterruptDescriptorTable_t *idt)
{
    for (size_t i = 0; i < IDT_TOTAL_VECTOR_COUNT; ++i)
        interrupt_descriptor_table_encode_flat_entry(&idt->entries[i], NULL, 0u, 0u);
}

static void interrupt_descriptor_table_install_vector_range(InterruptDescriptorTable_t *idt, size_t base,
                                                            const isr_fn_t *stubs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        interrupt_descriptor_table_encode_flat_entry(&idt->entries[base + i], (void *) stubs[i],
                                                     GDT_KERNEL_CODE_SELECTOR, IDT_KERNEL_INTERRUPT_GATE);
    }
}

////////////////////////////////////////////////////////////
// Public API functions of the IDT module
////////////////////////////////////////////////////////////

/**
 * @brief Initialize a flat 32-bit IDT with all 32 CPU exception handlers.
 *
 * @details Zeroes all 256 entries, then installs ISR stubs 0-47 as kernel
 *          interrupt gates (DPL=0, selector=0x08, flags=IDT_KERNEL_INTERRUPT_GATE).
 *
 * @param idt Pointer to the IDT structure to populate.
 */
void interrupt_descriptor_table_initialize(InterruptDescriptorTable_t *idt)
{
    if (!idt)
        return;

    interrupt_descriptor_table_clear(idt);
    interrupt_descriptor_table_install_vector_range(idt, 0u, idt_exception_stubs, IDT_EXCEPTION_VECTOR_COUNT);
    interrupt_descriptor_table_install_vector_range(idt, PIC_VECTOR_OFFSET_MASTER, idt_irq_stubs, IDT_IRQ_VECTOR_COUNT);

    /* Install IPI handlers */
    interrupt_descriptor_table_encode_flat_entry(&idt->entries[0x40u], (void *) isr64,
                                                 GDT_KERNEL_CODE_SELECTOR, IDT_KERNEL_INTERRUPT_GATE);

    /* Install user-callable syscall gate (int 0x80, DPL=3). */
    interrupt_descriptor_table_encode_flat_entry(&idt->entries[0x80u], (void *) isr128,
                                                 GDT_KERNEL_CODE_SELECTOR, IDT_USER_INTERRUPT_GATE);
}

/**
 * @brief Load and activate the IDT using LIDT.
 *
 * @details Constructs the IDTR and loads it.  Interrupts are NOT enabled
 *          here — call sti() explicitly after PIC initialization.
 *
 * @param idt Pointer to the initialized IDT structure.
 */
void interrupt_descriptor_table_load(InterruptDescriptorTable_t *idt)
{
    if (!idt)
        return;

    InterruptDescriptorTableRegister_t idtr;
    idtr.size = (uint16_t) (sizeof(InterruptDescriptorTable_t) - 1u);
    idtr.offset = (uint32_t) idt;

    idt_load(&idtr);
}
