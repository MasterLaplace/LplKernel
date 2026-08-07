#include <stddef.h>

#include <kernel/diag/telemetry.h>
#include <kernel/memory/backpressure.h>

static KernelBackpressureQueue_t backpressure_queues[KERNEL_BACKPRESSURE_MAX_QUEUES];
static uint32_t backpressure_queue_count = 0u;

static const char *backpressure_policy_name(KernelBackpressurePolicy_t policy)
{
    switch (policy)
    {
    case KERNEL_BACKPRESSURE_POLICY_DROP_CORRUPTS: return "drop_corrupts";
    case KERNEL_BACKPRESSURE_POLICY_DROP_TOLERATED:
    default: return "drop_tolerated";
    }
}

void kernel_backpressure_reset(void) { backpressure_queue_count = 0u; }

bool kernel_backpressure_register(const char *name, KernelBackpressurePolicy_t policy, uint32_t capacity_items,
                                  KernelBackpressureDropCountFunction_t drop_count)
{
    if (!name || !drop_count)
        return false;

    if (backpressure_queue_count >= KERNEL_BACKPRESSURE_MAX_QUEUES)
        return false;

    KernelBackpressureQueue_t *queue = &backpressure_queues[backpressure_queue_count];

    queue->name = name;
    queue->policy = policy;
    queue->capacity_items = capacity_items;
    queue->drop_count = drop_count;

    ++backpressure_queue_count;
    return true;
}

uint32_t kernel_backpressure_get_queue_count(void) { return backpressure_queue_count; }

const KernelBackpressureQueue_t *kernel_backpressure_get_queue(uint32_t index)
{
    if (index >= backpressure_queue_count)
        return NULL;

    return &backpressure_queues[index];
}

uint32_t kernel_backpressure_get_total_drop_count(void)
{
    uint32_t total = 0u;

    for (uint32_t index = 0u; index < backpressure_queue_count; ++index)
        total += backpressure_queues[index].drop_count();

    return total;
}

uint32_t kernel_backpressure_get_intolerant_drop_count(void)
{
    uint32_t total = 0u;

    for (uint32_t index = 0u; index < backpressure_queue_count; ++index)
    {
        if (backpressure_queues[index].policy != KERNEL_BACKPRESSURE_POLICY_DROP_CORRUPTS)
            continue;

        total += backpressure_queues[index].drop_count();
    }

    return total;
}

void kernel_backpressure_report(Serial_t *serial)
{
    if (!serial)
        return;

    for (uint32_t index = 0u; index < backpressure_queue_count; ++index)
    {
        const KernelBackpressureQueue_t *queue = &backpressure_queues[index];

        /* One record per queue rather than one record with a field per queue:
           the set of queues is a runtime fact, and a record whose field names
           change with the configuration is a record no reader can be written
           against. The queue name is a field VALUE here, never a field name. */
        kernel_telemetry_begin_record(serial, "backpressure");
        kernel_telemetry_write_text("queue", queue->name);
        kernel_telemetry_write_text("policy", backpressure_policy_name(queue->policy));
        kernel_telemetry_write_unsigned("capacity", queue->capacity_items);
        kernel_telemetry_write_unsigned("dropped", queue->drop_count());
        kernel_telemetry_end_record();
    }
}
