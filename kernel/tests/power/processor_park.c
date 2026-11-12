#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_identity.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/processor_park.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/testing/test.h>

#include <string.h>

KERNEL_TEST_SUITE(processor_park, KERNEL_TEST_STAGE_BOOTED);

#define PROCESSOR_PARK_TEST_QUICK_CYCLES      1000u    /**< The made-up threshold the judge tests give. */
#define PROCESSOR_PARK_TEST_WAIT_MICROSECONDS 1000000u /**< How long a parked processor may take to answer. */
#define PROCESSOR_PARK_TEST_C2_PORT           0x414u   /**< AMD's C2 port, the made-up ladders' depth 1. */
#define PROCESSOR_PARK_TEST_C3_PORT           0x415u   /**< AMD's C3 port, their depth 2. */

/**
 * @brief Judges @p reads reads alike, and says whether one of them refused the depth.
 */
static bool processor_park_test_judge_alike(ProcessorParkJudge_t *judge, uint32_t reads, bool requested,
                                            bool still_requested, uint64_t cycles)
{
    bool refused = false;

    for (uint32_t read = 0u; read < reads; ++read)
        refused |=
            kernel_processor_park_judge(judge, requested, still_requested, cycles, PROCESSOR_PARK_TEST_QUICK_CYCLES);
    return refused;
}

/**
 * @brief A ladder as an AMD processor builds it: C1 by MWAIT 0, then port 0x414 at depth 1 and 0x415 at depth 2.
 */
static ProcessorSleepLadder_t processor_park_test_amd_ladder(void)
{
    ProcessorSleepLadder_t ladder;

    memset(&ladder, 0, sizeof(ladder));
    ladder.available = 0x7u;
    ladder.depths[0] = (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT};
    ladder.depths[1] =
        (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_PORT, .port = PROCESSOR_PARK_TEST_C2_PORT};
    ladder.depths[2] =
        (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_PORT, .port = PROCESSOR_PARK_TEST_C3_PORT};
    return ladder;
}

/**
 * @brief A ladder as an Intel processor with deep MWAIT builds it: hints 0, 1 and 3, a gap at 2.
 */
static ProcessorSleepLadder_t processor_park_test_intel_ladder(void)
{
    ProcessorSleepLadder_t ladder;

    memset(&ladder, 0, sizeof(ladder));
    ladder.available = 0xBu;
    for (uint32_t depth = 0u; depth < 4u; ++depth)
        ladder.depths[depth] = (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_MONITOR_WAIT, .hint = depth};
    ladder.depths[2] = (ProcessorSleepDepth_t){0};
    return ladder;
}

/**
 * @brief Seven quick reads keep a port depth and the eighth refuses it: a read that returns at once with no interrupt
 *        requested did not stop the processor, and eight in a row are what a port nothing decodes gives.
 */
KERNEL_TEST(seven_quick_reads_keep_a_port_depth_and_the_eighth_refuses_it)
{
    ProcessorParkJudge_t judge = {0};

    kernel_test_check(test,
                      !processor_park_test_judge_alike(&judge, KERNEL_PROCESSOR_PARK_QUICK_LIMIT - 1u, false, false,
                                                       PROCESSOR_PARK_TEST_QUICK_CYCLES - 1u),
                      "seven quick reads keep it");
    kernel_test_check(test,
                      kernel_processor_park_judge(&judge, false, false, PROCESSOR_PARK_TEST_QUICK_CYCLES - 1u,
                                                  PROCESSOR_PARK_TEST_QUICK_CYCLES),
                      "the eighth refuses it");
}

/**
 * @brief A read as long as the threshold, or one an interrupt ended, is no quick read, and starts the streak again:
 *        the hardware may end a C-state for any reason once it slept (ACPI 6.6 §8.1.3), and an interrupt is what
 *        should end it.
 */
KERNEL_TEST(a_slow_read_or_one_an_interrupt_ended_starts_the_quick_streak_again)
{
    ProcessorParkJudge_t judge = {0};
    const uint64_t quick = PROCESSOR_PARK_TEST_QUICK_CYCLES - 1u;
    const uint32_t seven = KERNEL_PROCESSOR_PARK_QUICK_LIMIT - 1u;

    bool refused = processor_park_test_judge_alike(&judge, seven, false, false, quick);
    refused |= processor_park_test_judge_alike(&judge, 1u, false, false, PROCESSOR_PARK_TEST_QUICK_CYCLES);
    refused |= processor_park_test_judge_alike(&judge, seven, false, false, quick);
    kernel_test_check(test, !refused, "a read as long as the threshold breaks the streak");

    refused = processor_park_test_judge_alike(&judge, 1u, true, false, 0u);
    refused |= processor_park_test_judge_alike(&judge, seven, false, false, quick);
    kernel_test_check(test, !refused, "a quick read an interrupt ended breaks it too");
    kernel_test_check(test, processor_park_test_judge_alike(&judge, 1u, false, false, quick),
                      "and the eighth quick read in a row after it refuses");
}

/**
 * @brief Sixty-four reads in a row whose interrupt stays requested once interrupts are enabled refuse a port depth,
 *        and a read whose interrupt was taken starts that streak again: an interrupt never taken would make every
 *        read look ended by it, and the read would spin.
 */
KERNEL_TEST(reads_whose_interrupt_is_never_taken_refuse_a_port_depth)
{
    ProcessorParkJudge_t judge = {0};
    const uint32_t short_of_the_limit = KERNEL_PROCESSOR_PARK_STUCK_LIMIT - 1u;

    bool refused = processor_park_test_judge_alike(&judge, short_of_the_limit, true, true, 0u);
    refused |= processor_park_test_judge_alike(&judge, 1u, true, false, 0u);
    refused |= processor_park_test_judge_alike(&judge, short_of_the_limit, true, true, 0u);
    kernel_test_check(test, !refused, "a taken interrupt breaks the streak");
    kernel_test_check(test, processor_park_test_judge_alike(&judge, 1u, true, true, 0u),
                      "the sixty-fourth in a row refuses");
}

/**
 * @brief Without a clock no read is quick, so the guard refuses nothing on time alone.
 */
KERNEL_TEST(without_a_threshold_no_read_is_quick)
{
    ProcessorParkJudge_t judge = {0};
    bool refused = false;

    for (uint32_t read = 0u; read < 4u * KERNEL_PROCESSOR_PARK_QUICK_LIMIT; ++read)
        refused |= kernel_processor_park_judge(&judge, false, false, 0u, 0u);
    kernel_test_check(test, !refused, "a read of 0 cycles against a threshold of 0 is not quick");
}

/**
 * @brief A port depth is inherited when a validated verdict names its port and no refused one does; an MWAIT depth,
 *        whose port field is 0, never is, and a word with neither flag is no verdict.
 */
KERNEL_TEST(a_port_depth_is_inherited_where_a_validated_verdict_names_its_port_and_no_refused_one_does)
{
    const ProcessorSleepLadder_t amd = processor_park_test_amd_ladder();
    const ProcessorSleepLadder_t intel = processor_park_test_intel_ladder();
    const uint32_t validated_c2[] = {PROCESSOR_PARK_TEST_C2_PORT | PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED};
    const uint32_t both[] = {PROCESSOR_PARK_TEST_C3_PORT | PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED,
                             PROCESSOR_PARK_TEST_C2_PORT | PROCESSOR_SLEEP_PORT_VERDICT_REFUSED,
                             PROCESSOR_PARK_TEST_C2_PORT | PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED};
    const uint32_t bare[] = {PROCESSOR_PARK_TEST_C2_PORT};
    const uint32_t port_zero[] = {PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED};

    kernel_test_check(test, kernel_processor_park_inherited_depths(&amd, validated_c2, 1u) == 0x2u,
                      "a validated 0x414 gives depth 1 alone");
    kernel_test_check(test, kernel_processor_park_inherited_depths(&amd, both, 3u) == 0x4u,
                      "a refused verdict wins over a validated one for the same port");
    kernel_test_check(test, kernel_processor_park_inherited_depths(&amd, validated_c2, 0u) == 0u,
                      "no word, no inherited depth");
    kernel_test_check(test, kernel_processor_park_inherited_depths(&amd, bare, 1u) == 0u,
                      "a port without a verdict validates nothing");
    kernel_test_check(test, kernel_processor_park_inherited_depths(&intel, port_zero, 1u) == 0u,
                      "an MWAIT depth is no port depth");
}

/**
 * @brief A parked processor sleeps at the deepest depth it may enter: any MWAIT depth, gaps skipped; a port depth only
 *        inherited, not refused, and while the clock is known; C1 when nothing else is left.
 */
KERNEL_TEST(a_parked_processor_targets_the_deepest_depth_it_may_enter)
{
    const ProcessorSleepLadder_t amd = processor_park_test_amd_ladder();
    const ProcessorSleepLadder_t intel = processor_park_test_intel_ladder();
    ProcessorSleepLadder_t gap = amd;

    gap.available = 0x5u;
    gap.depths[1] = (ProcessorSleepDepth_t){0};

    kernel_test_check(test, kernel_processor_park_target(&intel, 0u, 0u, false) == 3u,
                      "Intel: hint 3, no verdict and no clock needed");
    kernel_test_check(test, kernel_processor_park_target(&amd, 0x6u, 0u, true) == 2u, "AMD: both inherited, C3");
    kernel_test_check(test, kernel_processor_park_target(&amd, 0x6u, 0x4u, true) == 1u, "C3 refused: C2");
    kernel_test_check(test, kernel_processor_park_target(&amd, 0x6u, 0x6u, true) == 0u, "both refused: C1");
    kernel_test_check(test, kernel_processor_park_target(&amd, 0x2u, 0u, true) == 1u, "C2 alone inherited: C2");
    kernel_test_check(test, kernel_processor_park_target(&amd, 0x6u, 0u, false) == 0u, "no clock: C1");
    kernel_test_check(test, kernel_processor_park_target(&amd, 0u, 0u, true) == 0u, "no verdict: C1");
    kernel_test_check(test, kernel_processor_park_target(&gap, 0x4u, 0u, true) == 2u, "a gap below is skipped");
    kernel_test_check(test, kernel_processor_park_target(&amd, 0x40u, 0u, true) == 0u,
                      "an inherited bit past the ladder is no depth");
}

/**
 * @brief Waits, boundedly, until @p slot's record shows more than @p turns turns, and @p vector reached it at least
 *        once when @p vector is not 0.
 * @return Whether it did.
 */
static bool processor_park_test_wait_for_turns(uint32_t slot, uint32_t turns, uint8_t vector)
{
    InterruptRequestDeadline_t deadline;
    ProcessorParkRecord_t record = {0};

    interrupt_request_deadline_start(&deadline, PROCESSOR_PARK_TEST_WAIT_MICROSECONDS);
    do
    {
        (void) kernel_processor_park_record(slot, &record);
        if (record.turns > turns && (vector == 0u || device_interrupt_count(vector, slot) >= 1u))
            return true;
        asmutils_pause();
    } while (!interrupt_request_deadline_passed(&deadline));
    return false;
}

/**
 * @brief Whether any verdict the bootstrap processor published validates a port.
 */
static bool processor_park_test_any_port_validated(void)
{
    uint32_t verdicts[PROCESSOR_SLEEP_PORT_VERDICTS];

    kernel_processor_sleep_port_verdicts(verdicts);
    for (uint32_t index = 0u; index < PROCESSOR_SLEEP_PORT_VERDICTS; ++index)
        if ((verdicts[index] & PROCESSOR_SLEEP_PORT_VERDICT_VALIDATED) != 0u)
            return true;
    return false;
}

/**
 * @brief Every online application processor parks in its own slot, and a reconsider reaches each and makes it take
 *        a turn; none reads a port no verdict validated, and no slot but theirs shows a park.
 *
 * @details The slot is the one the processor's own APIC ID register names: under -smp 4, three processors writing one
 *          slot leave two empty.
 */
KERNEL_TEST(every_application_processor_parks_in_its_own_slot_and_a_reconsider_reaches_each)
{
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    const bool validated = processor_park_test_any_port_validated();
    uint32_t turns[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC] = {0u};
    uint32_t parked = 0u;
    uint32_t elsewhere = 0u;
    uint32_t woken = 0u;
    uint32_t read_unvalidated = 0u;
    uint32_t applications = 0u;
    ProcessorParkRecord_t record;

    if (cpu_topology_get_online_cpu_count() < 2u)
    {
        kernel_test_skip(test, "no other processor is online: boot with -smp 2");
        return;
    }
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        const bool application = slot != here && cpu_topology_is_logical_slot_online(slot);

        applications += application ? 1u : 0u;
        if (application)
            parked += processor_park_test_wait_for_turns(slot, 0u, 0u) ? 1u : 0u;
        memset(&record, 0, sizeof(record));
        (void) kernel_processor_park_record(slot, &record);
        turns[slot] = record.turns;
        elsewhere += !application && record.turns != 0u ? 1u : 0u;
        read_unvalidated += application && !validated && record.port_reads != 0u ? 1u : 0u;
    }
    kernel_processor_park_reconsider();
    const uint8_t vector = kernel_processor_park_reconsider_vector();
    for (uint32_t slot = 0u; vector != 0u && slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
        if (slot != here && cpu_topology_is_logical_slot_online(slot))
            woken += processor_park_test_wait_for_turns(slot, turns[slot], vector) ? 1u : 0u;

    kernel_test_check(test, parked == applications, "every online application processor took a turn of its park");
    kernel_test_check(test, elsewhere == 0u, "no other slot shows a park");
    kernel_test_check(test, vector != 0u, "the reconsider has a vector");
    kernel_test_check(test, woken == applications, "the reconsider reached each and made it take a turn");
    kernel_test_check(test, read_unvalidated == 0u, "no parked processor read a port no verdict validated");
    kernel_test_measure(test, "applications", applications);
    kernel_test_measure(test, "parked", parked);
    kernel_test_measure(test, "woken", woken);
}

/**
 * @brief Waits, boundedly, until @p slot's guard refused every depth it inherited and its last sleep is no port read.
 * @return Whether it did; @p out_record holds the last copy either way.
 */
static bool processor_park_test_wait_for_a_refusal(uint32_t slot, ProcessorParkRecord_t *out_record)
{
    InterruptRequestDeadline_t deadline;

    interrupt_request_deadline_start(&deadline, PROCESSOR_PARK_TEST_WAIT_MICROSECONDS);
    do
    {
        (void) kernel_processor_park_record(slot, out_record);
        if ((out_record->refused & out_record->inherited) == out_record->inherited &&
            out_record->entry != PROCESSOR_SLEEP_ENTRY_PORT)
            return true;
        asmutils_pause();
    } while (!interrupt_request_deadline_passed(&deadline));
    return false;
}

/**
 * @brief The first port depth of the bootstrap processor's adopted ladder, or PROCESSOR_SLEEP_NO_DEPTH.
 */
static uint32_t processor_park_test_first_port_depth(void)
{
    ProcessorSleepDepth_t depth;

    for (uint32_t candidate = PROCESSOR_SLEEP_DEPTH_C1 + 1u; candidate < PROCESSOR_SLEEP_DEPTHS; ++candidate)
        if (kernel_processor_sleep_depth_of(candidate, &depth) && depth.entry == PROCESSOR_SLEEP_ENTRY_PORT)
            return candidate;
    return PROCESSOR_SLEEP_NO_DEPTH;
}

/**
 * @brief A parked processor given a validated port whose read does not stop it refuses it on its own and halts again:
 *        the bootstrap processor's verdict is taken, and the guard still judges each read.
 *
 * @details Hypervisor-only: under QEMU a read of the port fixture's 0x414 returns at once, which the guard must see; on
 *          a real processor it may well stop it, and keeping it would be right. The verdict is published the way a
 *          probe that found the read stops the processor publishes it. Needs the port fixture
 *          (tools/acpi/boot-cst.sh --ports) under KVM `-cpu host,+invtsc`, whose first two processors list 0x414; a
 *          processor whose slot lists no port takes none. Last of the suite: the refusal holds until the next boot.
 */
KERNEL_TEST(a_parked_processor_refuses_a_validated_port_whose_read_does_not_stop_it)
{
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    uint32_t turns[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC] = {0u};
    CpuIdentity_t identity;
    ProcessorParkRecord_t record;
    ProcessorParkRecord_t judged_record = {0};
    uint32_t applications = 0u;
    uint32_t looked = 0u;
    uint32_t refused = 0u;
    uint32_t inherited = 0u;
    uint32_t judged = 0u;

    cpu_identity_read(&identity);
    if (!identity.hypervisor)
    {
        kernel_test_skip(test, "a real processor: the port's read may well stop it");
        return;
    }
    if (cpu_topology_get_online_cpu_count() < 2u)
    {
        kernel_test_skip(test, "no other processor is online: boot with -smp 2");
        return;
    }
    kernel_processor_sleep_initialize();
    const uint32_t depth = processor_park_test_first_port_depth();
    if (depth == PROCESSOR_SLEEP_NO_DEPTH)
    {
        kernel_test_skip(test, "no port depth here: boot the port fixture under KVM -cpu host,+invtsc");
        return;
    }
    (void) kernel_processor_sleep_request_depth(depth);
    kernel_processor_sleep_begin_probe();
    kernel_processor_sleep_end_probe(true);
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
        if (kernel_processor_park_record(slot, &record))
            turns[slot] = record.turns;
    kernel_processor_park_reconsider();
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        if (slot == here || !cpu_topology_is_logical_slot_online(slot))
            continue;
        ++applications;
        looked += processor_park_test_wait_for_turns(slot, turns[slot], 0u) ? 1u : 0u;
        memset(&record, 0, sizeof(record));
        (void) kernel_processor_park_record(slot, &record);
        if (record.inherited == 0u)
            continue;
        ++inherited;
        refused += processor_park_test_wait_for_a_refusal(slot, &record) ? 1u : 0u;
        judged += record.quick >= KERNEL_PROCESSOR_PARK_QUICK_LIMIT && record.port_reads >= record.quick ? 1u : 0u;
        judged_record = record;
    }
    kernel_processor_sleep_initialize();
    kernel_processor_park_reconsider();

    kernel_test_check(test, looked == applications, "each looked at its target again");
    kernel_test_check(test, inherited != 0u, "one at least took the validated port");
    kernel_test_check(test, judged == inherited, "each that took it judged at least eight quick reads");
    kernel_test_check(test, refused == inherited, "and refused it, and sleeps by no port read");
    kernel_test_measure(test, "applications", applications);
    kernel_test_measure(test, "inherited", inherited);
    kernel_test_measure(test, "quick", judged_record.quick);
    kernel_test_measure(test, "port_reads", judged_record.port_reads);
    kernel_test_measure(test, "stuck", judged_record.stuck);
}
