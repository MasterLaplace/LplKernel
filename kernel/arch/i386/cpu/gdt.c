#include <kernel/cpu/gdt.h>
#include <stddef.h>
#include <string.h>

////////////////////////////////////////////////////////////
// External assembly functions
////////////////////////////////////////////////////////////

/**
 * @brief Load GDT using LGDT instruction (implemented in gdt.s)
 * @param gdtr Pointer to GDTR structure
 */
extern void gdt_load(const GlobalDescriptorTablePointer_t *gdtr);

/**
 * @brief Flush (reload) segment registers after loading GDT (implemented in gdt.s)
 */
extern void gdt_flush(void);

////////////////////////////////////////////////////////////
// Private functions of the GDT module
////////////////////////////////////////////////////////////

/**
 * @brief Helper to encode a GDT entry from base, limit, access byte, and flags
 *
 * @param entry Pointer to the GDT entry to encode
 * @param base 32-bit base address
 * @param limit 20-bit limit (will be clamped to 0xFFFFF if larger)
 * @param access Raw access byte (8 bits)
 * @param flags Raw flags byte (8 bits: high nibble = G/D/B/L/AVL, low nibble = limit[19:16])
 */
static inline void global_descriptor_table_encode_entry(GlobalDescriptorTableEntry_t *entry, uint32_t base,
                                                        uint32_t limit, uint8_t access, uint8_t flags)
{
    if (!entry)
        return;

    // Clamp limit to 20 bits
    if (limit > 0xFFFFF)
        limit = 0xFFFFF;

    // Encode limit (low 16 bits)
    entry->limit_low = (uint16_t) (limit & 0xFFFF);

    // Encode base (split across three fields)
    entry->base_low = (uint16_t) (base & 0xFFFF);
    entry->base_middle = (uint8_t) ((base >> 16) & 0xFF);
    entry->base_high = (uint8_t) ((base >> 24) & 0xFF);

    // Encode access byte using memcpy to avoid bitfield issues
    memcpy(&entry->access_byte, &access, sizeof(uint8_t));

    // Encode flags: high nibble from flags, low nibble from limit[19:16]
    uint8_t granularity_byte = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    memcpy(&entry->flags, &granularity_byte, sizeof(uint8_t));
}

// Helper to read current ESP in a single place (used for TSS initialization)
static inline uint32_t get_current_esp(void)
{
    uint32_t esp;
    asm volatile("movl %%esp, %0" : "=r"(esp));
    return esp;
}

// Initialize basic TSS fields (SS0, ESP0, I/O map base). Keeps init logic tidy and testable.
static void task_state_segment_initialize(TaskStateSegment_t *tss, uint16_t kernel_ss_selector)
{
    if (!tss)
        return;

    tss->segment_selector0 = kernel_ss_selector;
    tss->estack_pointer0 = get_current_esp();
    tss->io_map_base = (uint16_t) sizeof(TaskStateSegment_t);
}

////////////////////////////////////////////////////////////
// Public functions of the GDT module API
////////////////////////////////////////////////////////////

/**
 * @brief Initialize a flat 32-bit GDT with standard kernel/user segments
 *
 * This creates a GDT with:
 *  - Null descriptor (offset 0x00)
 *  - Kernel code segment (offset 0x08, DPL=0, base=0, limit=4GB)
 *  - Kernel data segment (offset 0x10, DPL=0, base=0, limit=4GB)
 *  - User code segment (offset 0x18, DPL=3, base=0, limit=4GB)
 *  - User data segment (offset 0x20, DPL=3, base=0, limit=4GB)
 *  - TSS segment (offset 0x28, system descriptor)
 *
 * @param gdt Pointer to the GDT structure to initialize
 */
void global_descriptor_table_initialize(GlobalDescriptorTable_t *gdt, TaskStateSegment_t *tss)
{
    if (!gdt || !tss)
        return;

    // Null Descriptor (required by CPU, offset 0x00)
    global_descriptor_table_encode_entry(&gdt->null_descriptor, 0, 0, 0, 0);

    // Kernel Mode Code Segment (offset 0x08, selector 0x08)
    // Base=0, Limit=0xFFFFF, Access=0x9A (Present, DPL=0, Code, Executable, Readable)
    // Flags=0xC (G=1 for 4KB granularity, D/B=1 for 32-bit, L=0, AVL=0)
    global_descriptor_table_encode_entry(&gdt->kernel_mode_code_segment, 0x00000000, 0xFFFFF, 0x9A, 0xC0);

    // Kernel Mode Data Segment (offset 0x10, selector 0x10)
    // Base=0, Limit=0xFFFFF, Access=0x92 (Present, DPL=0, Data, Writable)
    // Flags=0xC (G=1, D/B=1, L=0, AVL=0)
    global_descriptor_table_encode_entry(&gdt->kernel_mode_data_segment, 0x00000000, 0xFFFFF, 0x92, 0xC0);

    // User Mode Code Segment (offset 0x18, selector 0x18 | 3 = 0x1B for RPL=3)
    // Base=0, Limit=0xFFFFF, Access=0xFA (Present, DPL=3, Code, Executable, Readable)
    // Flags=0xC (G=1, D/B=1, L=0, AVL=0)
    global_descriptor_table_encode_entry(&gdt->user_mode_code_segment, 0x00000000, 0xFFFFF, 0xFA, 0xC0);

    // User Mode Data Segment (offset 0x20, selector 0x20 | 3 = 0x23 for RPL=3)
    // Base=0, Limit=0xFFFFF, Access=0xF2 (Present, DPL=3, Data, Writable)
    // Flags=0xC (G=1, D/B=1, L=0, AVL=0)
    global_descriptor_table_encode_entry(&gdt->user_mode_data_segment, 0x00000000, 0xFFFFF, 0xF2, 0xC0);

    // Task State Segment (offset 0x28, selector 0x28)
    // Base = address of TSS, Limit = sizeof(TSS)-1
    // Access=0x89 (Present, DPL=0, System, Type=9 for 32-bit TSS Available)
    // Flags=0x40 (D/B bit set to indicate 32-bit size; G=0 for byte granularity)
    global_descriptor_table_encode_entry(&gdt->task_state_segment, (uint32_t) tss,
                                         (uint32_t) sizeof(TaskStateSegment_t) - 1, 0x89, 0x40);

    task_state_segment_initialize(tss, (uint16_t) offsetof(GlobalDescriptorTable_t, kernel_mode_data_segment));
}

/**
 * @brief Load and activate a GDT
 *
 * This function sets up the GDTR, loads the GDT using LGDT, and reloads all segment registers.
 * After this call, the CPU will use the new GDT for all memory accesses.
 *
 * @param gdt Pointer to the initialized GDT structure
 */
void global_descriptor_table_load(GlobalDescriptorTable_t *gdt)
{
    if (!gdt)
        return;

    // Setup GDTR: limit = size of GDT - 1, base = address of first entry
    GlobalDescriptorTablePointer_t gdtr;
    gdtr.limit = sizeof(GlobalDescriptorTable_t) - 1;
    gdtr.base = (uint32_t) gdt;

    // Load GDT using LGDT instruction (assembly)
    gdt_load(&gdtr);

    // Flush segment registers (reload CS via far jump, reload DS/ES/FS/GS/SS)
    gdt_flush();

    // Load TSS using LTR instruction (assembly)
    const uint16_t tss_selector = (uint16_t) offsetof(GlobalDescriptorTable_t, task_state_segment);
    task_state_segment_load(tss_selector);
}
