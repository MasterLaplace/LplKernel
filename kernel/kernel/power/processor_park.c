#include <kernel/power/processor_park.h>

#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/spsc_ring.h>

#include <string.h>

#define PROCESSOR_PARK_MICROSECONDS_PER_SECOND 1000000u /**< Microseconds in the second a rate counts in. */

/**
 * @brief One slot's record, alone in its interference span: each parked processor writes its own at every wake-up,
 *        and the parks of a broadcast wake together.
 */
typedef struct __attribute__((aligned(KERNEL_SPSC_RING_INTERFERENCE_SIZE))) ProcessorParkSlot {
    ProcessorParkRecord_t record; /**< What the processor of this slot did. */
} ProcessorParkSlot_t;

static ProcessorParkSlot_t processor_park_slots[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC];
static ProcessorParkRecord_t processor_park_unrecorded; /**< Where a processor whose slot is past the table writes. */
static volatile uint32_t processor_park_quick_cycles_word = 0u;
static volatile uint8_t processor_park_vector = 0u;

/**
 * @brief Whether depth @p depth of @p ladder is available and entered by a port read.
 */
static bool processor_park_is_port_depth(const ProcessorSleepLadder_t *ladder, uint32_t depth)
{
    return ((ladder->available >> depth) & 1u) != 0u && ladder->depths[depth].entry == PROCESSOR_SLEEP_ENTRY_PORT;
}

/**
 * @brief Whether some word of @p verdicts names @p port with @p verdict.
 */
static bool processor_park_verdict_names(const uint32_t *verdicts, uint32_t count, uint16_t port, uint32_t verdict)
{
    for (uint32_t index = 0u; index < count; ++index)
        if ((verdicts[index] & PROCESSOR_SLEEP_PORT_VERDICT_PORT) == port && (verdicts[index] & verdict) != 0u)
            return true;
    return false;
}

/**
 * @brief The cycles of @p microseconds at @p hertz, at most what a 32-bit word holds.
 */
static uint32_t processor_park_cycles_of(uint32_t microseconds, uint64_t hertz)
{
    const uint64_t cycles = ((uint64_t) microseconds * hertz) / PROCESSOR_PARK_MICROSECONDS_PER_SECOND;

    return cycles > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) cycles;
}

/**
 * @brief The record the calling processor writes: its slot's, by the slot its own APIC ID names.
 */
static ProcessorParkRecord_t *processor_park_own_record(void)
{
    const uint32_t slot = cpu_topology_get_slot_for_apic_id(apic_get_id());

    return slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC ? &processor_park_slots[slot].record :
                                                         &processor_park_unrecorded;
}

/**
 * @brief The port depths of @p ladder the bootstrap processor's verdicts let a parked processor take, as they stand.
 */
static uint32_t processor_park_inherited_now(const ProcessorSleepLadder_t *ladder)
{
    uint32_t verdicts[PROCESSOR_SLEEP_PORT_VERDICTS];

    kernel_processor_sleep_port_verdicts(verdicts);
    return kernel_processor_park_inherited_depths(ladder, verdicts, PROCESSOR_SLEEP_PORT_VERDICTS);
}

/**
 * @brief Run on a parked processor the reconsider reached: nothing, since the interrupt alone ends its sleep and its
 *        next turn looks at the target again.
 */
static void processor_park_woken(void *context) { (void) context; }

/**
 * @brief Sleeps at @p depth, an MWAIT or HLT depth, in the step that enables interrupts.
 */
static void processor_park_sleep_at(const ProcessorSleepDepth_t *depth)
{
    if (depth->entry != PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT)
    {
        asmutils_enable_interrupts_and_halt();
        return;
    }
    asmutils_monitor((const void *) kernel_processor_sleep_idle_line(), 0u, 0u);
    asmutils_enable_interrupts_and_monitor_wait(kernel_processor_sleep_mwait_hint(depth->hint), 0u);
}

/**
 * @brief Reads @p depth's port, interrupts masked, then lets the interrupt that ended the read in, and judges the
 *        read.
 * @return Whether the read refuses the depth.
 */
static bool processor_park_read_port(const ProcessorSleepDepth_t *depth, ProcessorParkJudge_t *judge,
                                     uint32_t quick_cycles, ProcessorParkRecord_t *record)
{
    const uint64_t cycles = asmutils_timed_input_byte(depth->port);
    const bool requested = apic_interrupt_requested();

    asmutils_enable_interrupts();
    asmutils_no_operation();
    const bool still_requested = requested && apic_interrupt_requested();

    ++record->port_reads;
    record->quick += !requested && cycles < quick_cycles ? 1u : 0u;
    record->stuck += still_requested ? 1u : 0u;
    return kernel_processor_park_judge(judge, requested, still_requested, cycles, quick_cycles);
}

/**
 * @brief One turn of a park: interrupts masked, the target as the verdicts and the refusals stand, then a sleep there.
 * @return The depth this turn refused, as a bit; 0 when it refused none.
 */
static uint32_t processor_park_turn(const ProcessorSleepLadder_t *ladder, ProcessorParkJudge_t *judges,
                                    uint32_t refused, ProcessorParkRecord_t *record)
{
    asmutils_disable_interrupts();

    const uint32_t quick_cycles = processor_park_quick_cycles_word;
    const uint32_t inherited = processor_park_inherited_now(ladder);
    const uint32_t depth = kernel_processor_park_target(ladder, inherited, refused, quick_cycles != 0u);
    const ProcessorSleepDepth_t *entered = &ladder->depths[depth];

    record->depth = depth;
    record->entry = entered->entry;
    record->target = entered->entry == PROCESSOR_SLEEP_ENTRY_PORT ? (uint32_t) entered->port : entered->hint;
    record->inherited = inherited;
    record->refused = refused;
    ++record->turns;
    if (entered->entry != PROCESSOR_SLEEP_ENTRY_PORT)
    {
        processor_park_sleep_at(entered);
        return 0u;
    }
    return processor_park_read_port(entered, &judges[depth], quick_cycles, record) ? 1u << depth : 0u;
}

bool kernel_processor_park_judge(ProcessorParkJudge_t *judge, bool requested, bool still_requested, uint64_t cycles,
                                 uint64_t quick_cycles)
{
    const bool quick = !requested && cycles < quick_cycles;
    const bool stuck = requested && still_requested;

    judge->quick = quick ? judge->quick + 1u : 0u;
    judge->stuck = stuck ? judge->stuck + 1u : 0u;
    return judge->quick >= KERNEL_PROCESSOR_PARK_QUICK_LIMIT || judge->stuck >= KERNEL_PROCESSOR_PARK_STUCK_LIMIT;
}

uint32_t kernel_processor_park_inherited_depths(const ProcessorSleepLadder_t *ladder, const uint32_t *verdicts,
                                                uint32_t count)
{
    uint32_t inherited = 0u;

    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1 + 1u; depth < PROCESSOR_SLEEP_DEPTHS; ++depth)
    {
        const uint16_t port = ladder->depths[depth].port;

        if (processor_park_is_port_depth(ladder, depth) &&
            processor_park_verdict_names(verdicts, count, port, PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED) &&
            !processor_park_verdict_names(verdicts, count, port, PROCESSOR_SLEEP_PORT_VERDICT_REFUSED))
            inherited |= 1u << depth;
    }
    return inherited;
}

uint32_t kernel_processor_park_target(const ProcessorSleepLadder_t *ladder, uint32_t inherited, uint32_t refused,
                                      bool clock_known)
{
    const uint32_t ports_allowed = clock_known ? inherited & ~refused : 0u;

    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_DEEPEST; depth > PROCESSOR_SLEEP_DEPTH_C1; --depth)
    {
        if (((ladder->available >> depth) & 1u) == 0u)
            continue;
        if (!processor_park_is_port_depth(ladder, depth) || ((ports_allowed >> depth) & 1u) != 0u)
            return depth;
    }
    return PROCESSOR_SLEEP_DEPTH_C1;
}

void kernel_processor_park(void)
{
    ProcessorParkRecord_t *record = processor_park_own_record();
    ProcessorParkJudge_t judges[PROCESSOR_SLEEP_DEPTHS];
    ProcessorSleepLadderFacts_t facts;
    ProcessorSleepLadder_t ladder;
    uint32_t refused = 0u;

    memset(judges, 0, sizeof(judges));
    kernel_processor_sleep_read_ladder_facts(&facts);
    kernel_processor_sleep_build_ladder(&facts, &ladder);
    for (;;)
        refused |= processor_park_turn(&ladder, judges, refused, record);
}

void kernel_processor_park_reconsider(void)
{
    uint8_t vector = processor_park_vector;

    processor_park_quick_cycles_word =
        processor_park_cycles_of(KERNEL_PROCESSOR_PARK_QUICK_MICROSECONDS,
                                 advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz());
    if (vector == 0u && !device_interrupt_allocate(processor_park_woken, NULL, &vector))
        return;
    processor_park_vector = vector;
    if (advanced_pic_ipi_is_ready())
        (void) advanced_pic_ipi_send_fixed(0u, vector, ADVANCED_PIC_IPI_SHORT_ALL_EXCL);
}

uint8_t kernel_processor_park_reconsider_vector(void) { return processor_park_vector; }

uint32_t kernel_processor_park_quick_cycles(void) { return processor_park_quick_cycles_word; }

bool kernel_processor_park_record(uint32_t slot, ProcessorParkRecord_t *out_record)
{
    if (slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
        return false;

    const ProcessorParkRecord_t *record = &processor_park_slots[slot].record;

    out_record->turns = record->turns;
    out_record->depth = record->depth;
    out_record->entry = record->entry;
    out_record->target = record->target;
    out_record->inherited = record->inherited;
    out_record->refused = record->refused;
    out_record->port_reads = record->port_reads;
    out_record->quick = record->quick;
    out_record->stuck = record->stuck;
    return true;
}
