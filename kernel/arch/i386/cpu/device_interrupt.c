#include <kernel/cpu/apic.h>
#include <kernel/cpu/cpu_identity.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/isr.h>

#include <stddef.h>

/** Each vector's handler. */
static DeviceInterruptHandler_t device_interrupt_handlers[DEVICE_INTERRUPT_VECTOR_COUNT] = {NULL};

/** The context each handler receives. */
static void *device_interrupt_contexts[DEVICE_INTERRUPT_VECTOR_COUNT] = {NULL};

/** Interrupts per vector and per slot, each cell written by the processor of its slot alone. */
static volatile uint32_t device_interrupt_counts[DEVICE_INTERRUPT_VECTOR_COUNT][CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC] =
    {{0u}};

/**
 * @brief Counts the interrupt on this processor, runs the vector's handler, then ends the interrupt.
 */
static void device_interrupt_dispatch(const InterruptFrame_t *frame)
{
    const uint32_t index = frame->int_no - DEVICE_INTERRUPT_VECTOR_BASE;
    const uint32_t slot = cpu_topology_get_slot_for_apic_id(apic_get_id());

    if (index < DEVICE_INTERRUPT_VECTOR_COUNT)
    {
        if (slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
            ++device_interrupt_counts[index][slot];
        if (device_interrupt_handlers[index] != NULL)
            device_interrupt_handlers[index](device_interrupt_contexts[index]);
    }
    apic_send_eoi();
}

bool device_interrupt_allocate(DeviceInterruptHandler_t handler, void *context, uint8_t *out_vector)
{
    if (handler == NULL)
        return false;
    for (uint32_t index = 0u; index < DEVICE_INTERRUPT_VECTOR_COUNT; ++index)
    {
        if (device_interrupt_handlers[index] != NULL)
            continue;
        device_interrupt_contexts[index] = context;
        device_interrupt_handlers[index] = handler;
        *out_vector = (uint8_t) (DEVICE_INTERRUPT_VECTOR_BASE + index);
        interrupt_service_routine_register_handler(*out_vector, device_interrupt_dispatch);
        return true;
    }
    return false;
}

void device_interrupt_release(uint8_t vector)
{
    const uint32_t index = (uint32_t) vector - DEVICE_INTERRUPT_VECTOR_BASE;

    if (index >= DEVICE_INTERRUPT_VECTOR_COUNT)
        return;
    device_interrupt_handlers[index] = NULL;
    device_interrupt_contexts[index] = NULL;
}

uint32_t device_interrupt_count(uint8_t vector, uint32_t slot)
{
    const uint32_t index = (uint32_t) vector - DEVICE_INTERRUPT_VECTOR_BASE;

    if (index >= DEVICE_INTERRUPT_VECTOR_COUNT || slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
        return 0u;
    return device_interrupt_counts[index][slot];
}

bool device_interrupt_platform_delivers(void)
{
    CpuIdentity_t identity;

    if (!apic_is_x2apic_active())
        return true;
    cpu_identity_read(&identity);
    return identity.hypervisor;
}
