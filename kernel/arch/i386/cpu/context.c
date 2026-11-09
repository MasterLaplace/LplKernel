#include <kernel/config.h>
#include <kernel/cpu/context.h>
#include <kernel/drivers/serial.h>

#include <stddef.h>
#include <stdint.h>

#define CPU_CONTEXT_STACK_ALIGNMENT 16u /**< The System V i386 ABI's stack alignment at a call. */
#define CPU_CONTEXT_FRAME_WORDS     5u  /**< EDI, ESI, EBX and EBP as cpu_context_switch pops them, then its return. */
#define CPU_CONTEXT_FRAME_ARGUMENT  1u  /**< ESI holds the entry's argument for cpu_context_start. */
#define CPU_CONTEXT_FRAME_ENTRY     2u  /**< EBX holds the entry. */
#define CPU_CONTEXT_FRAME_RETURN    4u  /**< Where cpu_context_switch returns: cpu_context_start. */
#define CPU_CONTEXT_FRAME_DEPTH                                                                                        \
    32u /**< How far below the aligned top the frame starts: once the switch has popped it, the stack sits 12 bytes    \
             below that top, and cpu_context_start's push of the argument aligns it for the call. */

_Static_assert(offsetof(CpuContext_t, stack_pointer) == CPU_CONTEXT_OFFSET_STACK_POINTER,
               "context_switch.S reads the stack pointer there");
_Static_assert(offsetof(CpuContext_t, extended_state) == CPU_CONTEXT_OFFSET_EXTENDED_STATE,
               "context_switch.S reads the extended state area there");
_Static_assert(offsetof(CpuContext_t, save) == CPU_CONTEXT_OFFSET_SAVE, "context_switch.S reads the save there");

void cpu_context_report_returned_entry(void)
{
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: a context's entry returned, and its processor halts\n");
}

void cpu_context_adopt(CpuContext_t *context, void *extended_state, ExtendedStateSave_t save)
{
    context->stack_pointer = 0u;
    context->extended_state = extended_state;
    context->save = save;
    extended_state_prepare_area(extended_state, save);
    cpu_context_track_every_component_as_modified(context);
}

void cpu_context_prepare(CpuContext_t *context, void *stack_top, void *extended_state, ExtendedStateSave_t save,
                         CpuContextEntry_t entry, void *argument)
{
    const uintptr_t aligned_top = (uintptr_t) stack_top & ~(uintptr_t) (CPU_CONTEXT_STACK_ALIGNMENT - 1u);
    uint32_t *const frame = (uint32_t *) (aligned_top - CPU_CONTEXT_FRAME_DEPTH);

    for (uint32_t word = 0u; word < CPU_CONTEXT_FRAME_WORDS; ++word)
        frame[word] = 0u;
    frame[CPU_CONTEXT_FRAME_ARGUMENT] = (uint32_t) (uintptr_t) argument;
    frame[CPU_CONTEXT_FRAME_ENTRY] = (uint32_t) (uintptr_t) entry;
    frame[CPU_CONTEXT_FRAME_RETURN] = (uint32_t) (uintptr_t) cpu_context_start;
    cpu_context_adopt(context, extended_state, save);
    context->stack_pointer = (uint32_t) (uintptr_t) frame;
}
