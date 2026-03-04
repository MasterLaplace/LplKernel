#include <kernel/cpu/tss.h>

////////////////////////////////////////////////////////////
// Private functions of the TSS module
////////////////////////////////////////////////////////////

// Helper to read current ESP in a single place (used for TSS initialization)
static inline uint32_t get_current_esp(void)
{
    uint32_t esp;
    asm volatile("movl %%esp, %0" : "=r"(esp));
    return esp;
}

////////////////////////////////////////////////////////////
// Public API functions of the TSS module
////////////////////////////////////////////////////////////

void task_state_segment_initialize(TaskStateSegment_t *tss, uint16_t kernel_ss_selector)
{
    if (!tss)
        return;

    tss->segment_selector0 = kernel_ss_selector;
    tss->estack_pointer0 = get_current_esp();
    tss->io_map_base = (uint16_t) sizeof(TaskStateSegment_t);
}
