#include <kernel/core/reconciler.h>
#include <kernel/diag/telemetry.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/backpressure.h>
#include <kernel/memory/frame_arena.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/section_protection.h>

static KernelReconcilerDeclaration_t reconciler_declaration = {0};
static bool reconciler_is_declared = false;

/* The hot-loop violation counter is global and lives for the whole boot, and the
   smoke battery raises it on purpose — proving the guard fires is what that smoke
   is for. Comparing its absolute value against a budget would therefore report a
   kernel in drift because a test did its job. The baseline is taken when the
   declaration is adopted, and only the delta since is judged. This has been paid
   for once already, in the session that first wired the real-time guard. */
static uint32_t reconciler_real_time_violation_baseline = 0u;

static uint32_t reconciler_pass_count = 0u;
static uint32_t reconciler_drift_count = 0u;
static uint32_t reconciler_drift_mask = 0u;
static volatile uint32_t reconciler_periodic_pass_count = 0u;

/* Raises the invariant's bit and reports whether it was in drift, so a caller can
   count the failures of one pass without repeating the mask arithmetic. */
static uint32_t reconciler_evaluate(KernelReconcilerInvariant_t invariant, bool holds)
{
    if (holds)
        return 0u;

    reconciler_drift_mask |= (1u << (uint32_t) invariant);
    return 1u;
}

void kernel_reconciler_declare(const KernelReconcilerDeclaration_t *declaration)
{
    if (!declaration)
        return;

    reconciler_declaration = *declaration;
    reconciler_is_declared = true;
    reconciler_pass_count = 0u;
    reconciler_drift_count = 0u;
    reconciler_drift_mask = 0u;
    reconciler_real_time_violation_baseline = kernel_heap_get_hot_loop_violation_count();
}

static uint32_t reconciler_real_time_violations_since_declaration(void)
{
    const uint32_t current = kernel_heap_get_hot_loop_violation_count();

    if (current <= reconciler_real_time_violation_baseline)
        return 0u;

    return current - reconciler_real_time_violation_baseline;
}

bool kernel_reconciler_is_declared(void) { return reconciler_is_declared; }

uint32_t kernel_reconciler_check(void)
{
    if (!reconciler_is_declared)
        return 0u;

    uint32_t drifted = 0u;

    if (reconciler_declaration.require_section_protection)
    {
        drifted +=
            reconciler_evaluate(KERNEL_RECONCILER_INVARIANT_SECTION_PROTECTION, kernel_section_protection_is_active());
        drifted += reconciler_evaluate(KERNEL_RECONCILER_INVARIANT_READ_ONLY_PAGE_COUNT,
                                       kernel_section_protection_get_read_only_page_count() ==
                                           reconciler_declaration.read_only_page_count);
    }

    if (reconciler_declaration.require_write_protect)
    {
        drifted += reconciler_evaluate(KERNEL_RECONCILER_INVARIANT_WRITE_PROTECT,
                                       kernel_section_protection_write_protect_is_enabled());
    }

    /* Peak rather than current usage: the arena is reset every frame, so reading
       what it holds right now answers a question about this instant instead of
       about the run. */
    drifted += reconciler_evaluate(KERNEL_RECONCILER_INVARIANT_FRAME_ARENA,
                                   kernel_frame_arena_get_peak_used_bytes() <=
                                       reconciler_declaration.frame_arena_capacity_bytes);

    drifted += reconciler_evaluate(KERNEL_RECONCILER_INVARIANT_REAL_TIME,
                                   reconciler_real_time_violations_since_declaration() <=
                                       reconciler_declaration.real_time_violation_budget);

    drifted += reconciler_evaluate(KERNEL_RECONCILER_INVARIANT_QUEUE_INTEGRITY,
                                   kernel_backpressure_get_intolerant_drop_count() == 0u);

    ++reconciler_pass_count;

    if (drifted > 0u)
        ++reconciler_drift_count;

    return drifted;
}

void kernel_reconciler_check_periodic(void)
{
    (void) kernel_reconciler_check();
    ++reconciler_periodic_pass_count;
}

uint32_t kernel_reconciler_get_periodic_pass_count(void) { return reconciler_periodic_pass_count; }

bool kernel_reconciler_wait_for_periodic_pass(void)
{
    for (uint32_t spin = 0u; spin < KERNEL_RECONCILER_PERIODIC_WAIT_LIMIT; ++spin)
    {
        if (reconciler_periodic_pass_count > 0u)
            return true;

        asmutils_halt();
    }

    return reconciler_periodic_pass_count > 0u;
}

uint32_t kernel_reconciler_get_pass_count(void) { return reconciler_pass_count; }

uint32_t kernel_reconciler_get_drift_count(void) { return reconciler_drift_count; }

uint32_t kernel_reconciler_get_drift_mask(void) { return reconciler_drift_mask; }

const char *kernel_reconciler_get_invariant_name(KernelReconcilerInvariant_t invariant)
{
    switch (invariant)
    {
    case KERNEL_RECONCILER_INVARIANT_SECTION_PROTECTION: return "section_protection";
    case KERNEL_RECONCILER_INVARIANT_WRITE_PROTECT: return "write_protect";
    case KERNEL_RECONCILER_INVARIANT_READ_ONLY_PAGE_COUNT: return "read_only_page_count";
    case KERNEL_RECONCILER_INVARIANT_FRAME_ARENA: return "frame_arena";
    case KERNEL_RECONCILER_INVARIANT_REAL_TIME: return "real_time";
    case KERNEL_RECONCILER_INVARIANT_QUEUE_INTEGRITY: return "queue_integrity";
    case KERNEL_RECONCILER_INVARIANT_COUNT:
    default: return "unknown";
    }
}

void kernel_reconciler_report(Serial_t *serial)
{
    if (!serial)
        return;

    /* A drift count of zero proves nothing on its own: a reconciler that never ran
       reports exactly the same thing as one that ran ten thousand times and never
       saw a discrepancy. So the report waits, with a bound, for the periodic driver
       to prove itself — otherwise the liveness printed here would be a race with
       how fast the machine happened to boot. */
    const bool periodic_ran = kernel_reconciler_wait_for_periodic_pass();

    kernel_telemetry_begin_record(serial, "reconciler");
    kernel_telemetry_write_boolean("declared", reconciler_is_declared);
    kernel_telemetry_write_boolean("periodic_ran", periodic_ran);
    kernel_telemetry_write_unsigned("tick_passes", reconciler_periodic_pass_count);
    kernel_telemetry_write_unsigned("passes", reconciler_pass_count);
    kernel_telemetry_write_unsigned("drifts", reconciler_drift_count);
    kernel_telemetry_write_hexadecimal("drift_mask", reconciler_drift_mask);
    kernel_telemetry_write_unsigned("invariants", (uint32_t) KERNEL_RECONCILER_INVARIANT_COUNT);
    kernel_telemetry_write_unsigned("arena_peak", kernel_frame_arena_get_peak_used_bytes());
    kernel_telemetry_write_unsigned("arena_capacity", reconciler_declaration.frame_arena_capacity_bytes);
    kernel_telemetry_write_unsigned("real_time_violations", reconciler_real_time_violations_since_declaration());
    kernel_telemetry_write_unsigned("queues", kernel_backpressure_get_queue_count());
    kernel_telemetry_write_unsigned("queue_drops", kernel_backpressure_get_total_drop_count());
    kernel_telemetry_write_unsigned("queue_corrupting_drops", kernel_backpressure_get_intolerant_drop_count());
    kernel_telemetry_end_record();
}
