#include <kernel/cpu/tss.h>

/**
 * @brief Reads the current ESP, in the single place TSS initialisation takes it from.
 * @return The stack pointer.
 */
static inline uint32_t get_current_esp(void) { return asmutils_get_current_stack_pointer(); }

void task_state_segment_initialize(TaskStateSegment_t *tss, uint16_t kernel_ss_selector)
{
    if (!tss)
        return;

    tss->segment_selector0 = kernel_ss_selector;
    tss->estack_pointer0 = get_current_esp();
    tss->io_map_base = (uint16_t) sizeof(TaskStateSegment_t);
}
