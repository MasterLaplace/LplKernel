#include <kernel/cpu/gdt.h>
#include <stddef.h>
#include <string.h>

////////////////////////////////////////////////////////////
// External assembly functions (gdt_load.s)
////////////////////////////////////////////////////////////

/**
 * @brief Load a GDT using the LGDT instruction.
 * @param gdtr Pointer to a GDTR structure (limit + base).
 */
extern void gdt_load(const GlobalDescriptorTablePointer_t *gdtr);

/**
 * @brief Reload all segment registers after a GDT change.
 * @details Performs a far jump to reload CS, then reloads DS/ES/FS/GS/SS.
 */
extern void gdt_flush(void);

////////////////////////////////////////////////////////////
// Private helpers functions of the GDT module
////////////////////////////////////////////////////////////

/**
 * @brief Encode a single GDT entry from its constituent parts.
 *
 * @details Splits the 32-bit base across three fields, the 20-bit limit
 *          across limit_low and the flags nibble.  The access byte and
 *          flags byte are written via memcpy to avoid bitfield portability
 *          issues.
 *
 * @param entry  Target GDT entry to populate.
 * @param base   32-bit segment base address.
 * @param limit  20-bit segment limit (clamped if larger).
 * @param access Raw access byte (P, DPL, S, Type fields).
 * @param flags  Raw flags byte (G, D/B, L, AVL in high nibble).
 */
static inline void global_descriptor_table_encode_entry(GlobalDescriptorTableEntry_t *entry, uint32_t base,
                                                        uint32_t limit, uint8_t access, uint8_t flags)
{
    if (!entry)
        return;

    uint32_t clamped_limit = (limit > 0xFFFFF) ? 0xFFFFF : limit;

    entry->limit_low = (uint16_t) (clamped_limit & 0xFFFF);
    entry->base_low = (uint16_t) (base & 0xFFFF);
    entry->base_middle = (uint8_t) ((base >> 16u) & 0xFF);
    entry->base_high = (uint8_t) ((base >> 24u) & 0xFF);

    memcpy(&entry->access_byte, &access, sizeof(uint8_t));

    uint8_t granularity_byte = ((clamped_limit >> 16) & 0x0F) | (flags & 0xF0);
    memcpy(&entry->flags, &granularity_byte, sizeof(uint8_t));
}

////////////////////////////////////////////////////////////
// Public API functions of the GDT module
////////////////////////////////////////////////////////////

/**
 * @brief Initialize a flat 32-bit GDT with kernel/user segments and TSS.
 *
 * @details Creates six entries at fixed selectors:
 *          - 0x00: Null descriptor (required by CPU)
 *          - 0x08: Kernel code (DPL=0, base=0, limit=4 GB, access=0x9A)
 *          - 0x10: Kernel data (DPL=0, base=0, limit=4 GB, access=0x92)
 *          - 0x18: User code   (DPL=3, base=0, limit=4 GB, access=0xFA)
 *          - 0x20: User data   (DPL=3, base=0, limit=4 GB, access=0xF2)
 *          - 0x28: TSS         (system descriptor, byte granularity)
 *
 * @param gdt Pointer to the GDT structure to populate.
 * @param tss Pointer to the TSS used for the TSS descriptor.
 */
void global_descriptor_table_initialize(GlobalDescriptorTable_t *gdt, TaskStateSegment_t *tss)
{
    if (!gdt || !tss)
        return;

    global_descriptor_table_encode_entry(&gdt->null_descriptor, 0, 0, 0, 0);
    global_descriptor_table_encode_entry(&gdt->kernel_mode_code_segment, 0, 0xFFFFF, 0x9A, 0xC0);
    global_descriptor_table_encode_entry(&gdt->kernel_mode_data_segment, 0, 0xFFFFF, 0x92, 0xC0);
    global_descriptor_table_encode_entry(&gdt->user_mode_code_segment, 0, 0xFFFFF, 0xFA, 0xC0);
    global_descriptor_table_encode_entry(&gdt->user_mode_data_segment, 0, 0xFFFFF, 0xF2, 0xC0);
    global_descriptor_table_encode_entry(&gdt->task_state_segment, (uint32_t) tss,
                                         (uint32_t) sizeof(TaskStateSegment_t) - 1, 0x89, 0x40);

    task_state_segment_initialize(tss, (uint16_t) offsetof(GlobalDescriptorTable_t, kernel_mode_data_segment));
}

/**
 * @brief Load and activate a GDT.
 *
 * @details Constructs the GDTR, loads the GDT via LGDT, reloads all
 *          segment registers, and loads the TSS via LTR.
 *
 * @param gdt Pointer to the initialized GDT structure.
 */
void global_descriptor_table_load(GlobalDescriptorTable_t *gdt)
{
    if (!gdt)
        return;

    GlobalDescriptorTablePointer_t gdtr;
    gdtr.limit = sizeof(GlobalDescriptorTable_t) - 1;
    gdtr.base = (uint32_t) gdt;

    gdt_load(&gdtr);
    gdt_flush();

    const uint16_t tss_selector = (uint16_t) offsetof(GlobalDescriptorTable_t, task_state_segment);
    task_state_segment_load(tss_selector);
}
