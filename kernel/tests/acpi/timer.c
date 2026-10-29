#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_power_management_timer, KERNEL_TEST_STAGE_INITIALIZATION);

#define TIMER_PORT          0x608u      /**< Where the made-up platform's timer lies, as on QEMU's q35. */
#define TIMER_HERTZ         3579545u    /**< What a timer counts per second, written out apart from the code. */
#define TIMER_TICK          100000u     /**< A step of the made-up host's clock, 10 ms in units of 100 ns. */
#define TIMER_ACCESS        1000u       /**< Nanoseconds each read of the made-up host costs. */
#define TIMER_FAST_ACCESS   100u        /**< Nanoseconds a read costs where a wait's precision is checked. */
#define TIMER_SLOW_ACCESS   10000u      /**< Nanoseconds a read costs on a slow bus, 40 µs a poll. */
#define TIMER_MATRIX_ACCESS 4000u       /**< Nanoseconds a read costs where many timers are found in a row. */
#define TIMER_MILLISECOND   1000000u    /**< A millisecond of the made-up machine, in nanoseconds. */
#define TIMER_LOW_BITS      0x00FFFFFFu /**< The 24 bits every such timer runs. */
#define TIMER_ALL_BITS      0xFFFFFFFFu /**< A timer of 32 bits, TMR_VAL_EXT set. */
#define TIMER_EIGHT_BITS    0x000000FFu /**< A port that decodes only 8 bits of the count. */
#define TIMER_OUT_OF_ORDER  0x00400000u /**< What a read caught in an increment adds to the count. */
#define TIMER_NO_EVENT      0u          /**< A read or an edge number that names none. */
#define TIMER_STUTTER       6u          /**< Port reads a stuttering timer holds each count for: two triples. */
#define TIMER_WRAP_FIRST    20u /**< Counts before the wrap a timer starts at, fewest: it wraps in the first checks. */
#define TIMER_WRAP_LAST     60u /**< Most: two checks of reads later, so that one of them sees the wrap inside it. */
#define TIMER_STATE(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_##name

/**
 * @brief A made-up machine whose time moves with each read made of it, its timer counting at its own
 *        rate and its clock stepping by whole ticks, as the kernel's does.
 */
typedef struct TimerMachine {
    uint64_t nanoseconds;       /**< Time on the machine. */
    uint32_t access;            /**< Nanoseconds each read of a port or of the clock adds. */
    uint64_t hertz;             /**< What its timer counts per second. */
    uint32_t start;             /**< The timer's count at time 0. */
    uint32_t bits;              /**< The bits of the count the port gives. */
    uint32_t tick;              /**< The step of the clock in units of 100 ns; 0 for a clock that stands still. */
    uint32_t late_edge;         /**< The step of the clock seen late, counted from time 0, or TIMER_NO_EVENT. */
    uint32_t late_by;           /**< How late, in nanoseconds. */
    uint32_t reads;             /**< Reads of the timer's port. */
    uint32_t out_of_order;      /**< The read that gives a count caught in an increment, or TIMER_NO_EVENT. */
    uint32_t stops_after;       /**< The read from which the timer holds still, or TIMER_NO_EVENT. */
    uint32_t stopped_count;     /**< What it holds then. */
    uint32_t goes_back_at;      /**< The read from which the timer counts back_by less, or TIMER_NO_EVENT. */
    uint32_t back_by;           /**< By how much. */
    bool counts_down;           /**< The timer goes back instead of forward. */
    bool stutters;              /**< The timer holds each count for TIMER_STUTTER reads of its port. */
    uint64_t held_nanoseconds;  /**< The time a stuttering timer gives the count of. */
    uint32_t host_stalls;       /**< Calls to the host's own stall. */
    uint32_t host_microseconds; /**< Microseconds the host's own stall waited. */
} TimerMachine_t;

static TimerMachine_t timer_machine;
static AdvancedConfigurationAndPowerInterfaceFixedDescription_t timer_fadt;
static AdvancedConfigurationAndPowerInterfacePowerManagementTimer_t timer_found;

/**
 * @brief What the made-up timer counts at @p nanoseconds of the machine's time.
 */
static uint32_t timer_count(const TimerMachine_t *machine, uint64_t nanoseconds)
{
    const uint32_t counted = (uint32_t) ((nanoseconds * machine->hertz) / 1000000000ull);

    return machine->counts_down ? machine->start - counted : machine->start + counted;
}

/**
 * @brief The made-up host's port read: the timer's port gives its count, any other all ones.
 */
static uint32_t timer_port_read(void *context, uint16_t port, uint8_t width)
{
    TimerMachine_t *machine = context;
    uint32_t count;

    (void) width;
    machine->nanoseconds += machine->access;
    if (port != TIMER_PORT)
        return TIMER_ALL_BITS;
    ++machine->reads;
    if (machine->stops_after != TIMER_NO_EVENT && machine->reads >= machine->stops_after)
        return machine->stopped_count & machine->bits;
    if (machine->stutters && (machine->reads - 1u) % TIMER_STUTTER == 0u)
        machine->held_nanoseconds = machine->nanoseconds;
    count = timer_count(machine, machine->stutters ? machine->held_nanoseconds : machine->nanoseconds);
    if (machine->stops_after != TIMER_NO_EVENT && machine->reads + 1u == machine->stops_after)
        machine->stopped_count = count;
    if (machine->goes_back_at != TIMER_NO_EVENT && machine->reads >= machine->goes_back_at)
        count -= machine->back_by;
    if (machine->reads == machine->out_of_order)
        count += TIMER_OUT_OF_ORDER;
    return count & machine->bits;
}

/**
 * @brief The made-up host's clock, in units of 100 ns, stepping by whole ticks, one of which it may
 *        show late.
 */
static uint64_t timer_clock_100ns(void *context)
{
    TimerMachine_t *machine = context;
    uint64_t edge;

    machine->nanoseconds += machine->access;
    if (machine->tick == 0u)
        return 0u;
    edge = machine->nanoseconds / (machine->tick * 100ull);
    if (machine->late_edge != TIMER_NO_EVENT && edge == machine->late_edge &&
        machine->nanoseconds - edge * machine->tick * 100ull < machine->late_by)
        edge -= 1u;
    return edge * machine->tick;
}

/**
 * @brief The made-up host's own stall: it waits exactly what it is asked, and counts it.
 */
static uint32_t timer_host_stall(void *context, uint32_t microseconds)
{
    TimerMachine_t *machine = context;

    machine->nanoseconds += (uint64_t) microseconds * 1000u;
    ++machine->host_stalls;
    machine->host_microseconds += microseconds;
    return microseconds;
}

static const AdvancedConfigurationAndPowerInterfaceHost_t timer_host = {
    .context = &timer_machine,
    .port_read = timer_port_read,
    .stall_microseconds = timer_host_stall,
    .clock_100ns = timer_clock_100ns,
};

/**
 * @brief Resets the made-up machine to a 24-bit timer counting 3.579545 MHz from @p start, a clock of
 *        10 ms ticks and reads of 1 µs, and the FADT to one that names the timer's port.
 */
static void timer_open(uint32_t start)
{
    memset(&timer_machine, 0, sizeof(timer_machine));
    timer_machine.access = TIMER_ACCESS;
    timer_machine.hertz = TIMER_HERTZ;
    timer_machine.start = start;
    timer_machine.bits = TIMER_LOW_BITS;
    timer_machine.tick = TIMER_TICK;

    memset(&timer_fadt, 0, sizeof(timer_fadt));
    timer_fadt.present = true;
    timer_fadt.revision = 3u;
    timer_fadt.power_management_timer_block.address = TIMER_PORT;
    timer_fadt.power_management_timer_block.space =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_INPUT_OUTPUT;
    timer_fadt.power_management_timer_block.bit_width = 32u;
}

/**
 * @brief Moves the made-up timer so that it counts @p count now.
 */
static void timer_place(uint32_t count)
{
    timer_machine.start = count - (uint32_t) ((timer_machine.nanoseconds * timer_machine.hertz) / 1000000000ull);
}

/**
 * @brief Whether @p name is @p expected.
 */
static bool timer_name_is(const char *name, const char *expected)
{
    return strlen(name) == strlen(expected) && memcmp(name, expected, strlen(expected)) == 0;
}

/**
 * @brief Looks for the made-up machine's timer.
 */
static uint8_t timer_find(void)
{
    advanced_configuration_and_power_interface_find_power_management_timer(&timer_host, &timer_fadt, &timer_found);
    return timer_found.state;
}

/**
 * @brief Waits @p microseconds on the timer last found, and gives the nanoseconds that passed on the
 *        machine meanwhile.
 */
static uint64_t timer_wait(uint32_t microseconds, uint32_t *out_waited)
{
    const uint64_t before = timer_machine.nanoseconds;

    *out_waited = advanced_configuration_and_power_interface_power_management_timer_stall(&timer_host, &timer_found,
                                                                                          microseconds);
    return timer_machine.nanoseconds - before;
}

/**
 * @brief Finds the made-up machine's timer, then waits @p microseconds on it, as timer_wait does.
 */
static uint64_t timer_stall(uint32_t microseconds, uint32_t *out_waited)
{
    (void) timer_find();
    return timer_wait(microseconds, out_waited);
}

/**
 * @brief A platform without a FADT, a hardware-reduced one, one without PM_TMR_BLK, and one whose
 *        PM_TMR_BLK lies in memory or past the last port have no timer to wait on, and no port is read.
 */
KERNEL_TEST(a_platform_without_a_reachable_timer_has_none_and_reads_no_port)
{
    uint8_t absent_without_fadt;
    uint8_t absent_when_reduced;
    uint8_t absent_without_block;
    uint8_t unreachable_in_memory;
    uint8_t unreachable_past_the_ports;

    timer_open(0u);
    timer_fadt.present = false;
    absent_without_fadt = timer_find();
    timer_open(0u);
    timer_fadt.hardware_reduced = true;
    absent_when_reduced = timer_find();
    timer_open(0u);
    timer_fadt.power_management_timer_block.address = 0u;
    absent_without_block = timer_find();
    timer_open(0u);
    timer_fadt.power_management_timer_block.space = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_MEMORY;
    unreachable_in_memory = timer_find();
    timer_open(0u);
    timer_fadt.power_management_timer_block.address = 0xFFFEu;
    unreachable_past_the_ports = timer_find();

    kernel_test_check(test, absent_without_fadt == TIMER_STATE(ABSENT), "no FADT: absent");
    kernel_test_check(test, absent_when_reduced == TIMER_STATE(ABSENT), "a hardware-reduced FADT: absent");
    kernel_test_check(test, absent_without_block == TIMER_STATE(ABSENT), "no PM_TMR_BLK: absent");
    kernel_test_check(test, unreachable_in_memory == TIMER_STATE(UNREACHABLE), "a PM_TMR_BLK in memory: unreachable");
    kernel_test_check(test, unreachable_past_the_ports == TIMER_STATE(UNREACHABLE),
                      "4 bytes from 0xFFFE pass the last port: unreachable");
    kernel_test_check(test, timer_machine.reads == 0u && timer_found.check_reads == 0u, "and no port was read");
}

/**
 * @brief A timer of 24 bits, and one of 32 bits that crosses 2^24, that count 3.579545 MHz are usable,
 *        their rate a thousand thousandths of it within the 1 µs a read costs, wherever the wrap falls
 *        among the reads of the checks; the checks count their reads, and last from time 0 to the step of
 *        the clock 80 ms after the first one, 90 ms.
 */
KERNEL_TEST(a_timer_that_counts_3_579545_mhz_is_usable)
{
    uint8_t low_state;
    uint32_t low_rate;
    uint32_t low_reads;
    uint32_t low_microseconds;
    bool every_wrap_usable = true;
    uint8_t wide_state;

    for (uint32_t before_wrap = TIMER_WRAP_FIRST; before_wrap <= TIMER_WRAP_LAST; ++before_wrap)
    {
        timer_open(TIMER_LOW_BITS - before_wrap);
        every_wrap_usable = every_wrap_usable && timer_find() == TIMER_STATE(USABLE);
    }
    timer_open(0u);
    low_state = timer_find();
    low_rate = timer_found.rate_per_mille;
    low_reads = timer_found.check_reads;
    low_microseconds = timer_found.check_microseconds;
    timer_open(TIMER_LOW_BITS - TIMER_WRAP_FIRST);
    timer_machine.bits = TIMER_ALL_BITS;
    wide_state = timer_find();

    kernel_test_check(test, every_wrap_usable, "a 24-bit timer that wraps during any check is usable");
    kernel_test_check(test, low_state == TIMER_STATE(USABLE), "a 24-bit timer is usable");
    kernel_test_check(test, timer_found.port == TIMER_PORT, "its port is PM_TMR_BLK's");
    kernel_test_check(test, low_rate >= 999u && low_rate <= 1001u, "its rate is 3.579545 MHz within a read");
    kernel_test_check(test, low_reads == timer_machine.reads, "the checks count every read of the port");
    kernel_test_check(test, low_microseconds == 90000u, "and last 90 ms of the clock");
    kernel_test_check(test, wide_state == TIMER_STATE(USABLE), "a 32-bit timer that crosses 2^24 is usable");
    kernel_test_measure(test, "rate_per_mille", low_rate);
    kernel_test_measure(test, "check_reads", low_reads);
    kernel_test_measure(test, "check_us", low_microseconds);
}

/**
 * @brief A step of the clock seen 1 ms late leaves an exact timer usable; one seen 5 ms late spoils the
 *        first measurement only, and the second holds; a timer 6 % fast keeps the wrong rate through
 *        both measurements.
 */
KERNEL_TEST(a_tick_seen_late_spoils_one_measurement_not_the_timer)
{
    uint8_t one_late;
    uint8_t five_late;
    uint32_t five_late_rate;
    uint8_t fast;
    uint32_t fast_rate;

    timer_open(0u);
    timer_machine.late_edge = 1u;
    timer_machine.late_by = TIMER_MILLISECOND;
    one_late = timer_find();
    timer_open(0u);
    timer_machine.late_edge = 1u;
    timer_machine.late_by = 5u * TIMER_MILLISECOND;
    five_late = timer_find();
    five_late_rate = timer_found.rate_per_mille;
    timer_open(0u);
    timer_machine.hertz = TIMER_HERTZ * 106ull / 100u;
    fast = timer_find();
    fast_rate = timer_found.rate_per_mille;

    kernel_test_check(test, one_late == TIMER_STATE(USABLE), "a tick 1 ms late leaves the timer usable");
    kernel_test_check(test, five_late == TIMER_STATE(USABLE), "a tick 5 ms late spoils one measurement");
    kernel_test_check(test, five_late_rate >= 999u && five_late_rate <= 1001u, "and the next one holds");
    kernel_test_check(test, fast == TIMER_STATE(WRONG_RATE) && fast_rate >= 1059u && fast_rate <= 1061u,
                      "a timer 6 % fast keeps the wrong rate");
    kernel_test_measure(test, "five_late_rate", five_late_rate);
}

/**
 * @brief With one step of the clock seen 1 or 2 ms late at either end of either measurement, no timer
 *        8 % fast or slow is found usable, and an exact one always is.
 */
KERNEL_TEST(one_tick_seen_late_lets_no_timer_eight_percent_off_through)
{
    const uint32_t edges[] = {1u, 9u, 10u, 18u};
    const uint32_t per_hundred[] = {92u, 100u, 108u};
    uint32_t off_usable = 0u;
    uint32_t exact_refused = 0u;

    for (uint32_t edge = 0u; edge < sizeof(edges) / sizeof(edges[0]); ++edge)
        for (uint32_t late = 1u; late <= 2u; ++late)
            for (uint32_t rate = 0u; rate < sizeof(per_hundred) / sizeof(per_hundred[0]); ++rate)
            {
                timer_open(0u);
                timer_machine.access = TIMER_MATRIX_ACCESS;
                timer_machine.hertz = TIMER_HERTZ * (uint64_t) per_hundred[rate] / 100u;
                timer_machine.late_edge = edges[edge];
                timer_machine.late_by = late * TIMER_MILLISECOND;

                const bool usable = timer_find() == TIMER_STATE(USABLE);

                off_usable += (per_hundred[rate] != 100u && usable) ? 1u : 0u;
                exact_refused += (per_hundred[rate] == 100u && !usable) ? 1u : 0u;
            }

    kernel_test_check(test, off_usable == 0u, "no timer 8 % off is found usable");
    kernel_test_check(test, exact_refused == 0u, "and every exact one is");
    kernel_test_measure(test, "off_usable", off_usable);
}

/**
 * @brief A timer that holds still is stopped after a bounded number of reads; one that goes back, from
 *        the start or at its sixth check, is inconsistent; one 6 % fast or slow has the wrong rate, and
 *        one 4 % fast or slow does not.
 */
KERNEL_TEST(a_timer_that_holds_still_goes_back_or_runs_off_rate_is_not_usable)
{
    uint8_t stopped;
    uint32_t stopped_reads;
    uint8_t backward;
    uint8_t back_at_sixth;
    uint8_t fast;
    uint8_t slow;
    uint8_t near_fast;
    uint8_t near_slow;

    timer_open(0x1234u);
    timer_machine.stops_after = 1u;
    stopped = timer_find();
    stopped_reads = timer_machine.reads;
    timer_open(0x00800000u);
    timer_machine.counts_down = true;
    backward = timer_find();
    timer_open(0x00100000u);
    timer_machine.goes_back_at = 5u * 6u + 4u;
    timer_machine.back_by = 0x10000u;
    back_at_sixth = timer_find();
    timer_open(0u);
    timer_machine.hertz = TIMER_HERTZ * 106ull / 100u;
    fast = timer_find();
    timer_open(0u);
    timer_machine.hertz = TIMER_HERTZ * 94ull / 100u;
    slow = timer_find();
    timer_open(0u);
    timer_machine.hertz = TIMER_HERTZ * 104ull / 100u;
    near_fast = timer_find();
    timer_open(0u);
    timer_machine.hertz = TIMER_HERTZ * 96ull / 100u;
    near_slow = timer_find();

    kernel_test_check(test, stopped == TIMER_STATE(STOPPED), "a timer that holds still is stopped");
    kernel_test_check(test,
                      stopped_reads <=
                          3u * (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_READ_CHECKS + 1u),
                      "after at most the reads of one check, three port reads each");
    kernel_test_check(test, backward == TIMER_STATE(INCONSISTENT), "a timer that goes back is inconsistent");
    kernel_test_check(test, back_at_sixth == TIMER_STATE(INCONSISTENT),
                      "and so is one that goes back at its sixth check");
    kernel_test_check(test, fast == TIMER_STATE(WRONG_RATE) && slow == TIMER_STATE(WRONG_RATE),
                      "6 % fast or slow is the wrong rate");
    kernel_test_check(test, near_fast == TIMER_STATE(USABLE) && near_slow == TIMER_STATE(USABLE),
                      "4 % fast or slow is usable");
    kernel_test_measure(test, "stopped_reads", stopped_reads);
}

/**
 * @brief A clock that stands still, read fast, leaves the rate unchecked once it was read
 *        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_REFERENCE_POLLS times; read slowly,
 *        it lets the timer count half a second first, and reads as a timer that ran ahead, as one decoded
 *        on 8 bits or a hundred times too fast does: the timer and the clock alone cannot tell them apart.
 */
KERNEL_TEST(a_still_clock_reads_as_no_reference_or_ran_ahead_by_the_cost_of_a_read)
{
    uint8_t still_clock;
    uint64_t still_nanoseconds;
    uint8_t slow_still_clock;
    uint64_t slow_still_nanoseconds;
    uint8_t eight_bits;
    uint8_t hundredfold;

    timer_open(0u);
    timer_machine.tick = 0u;
    still_clock = timer_find();
    still_nanoseconds = timer_machine.nanoseconds;
    timer_open(0u);
    timer_machine.tick = 0u;
    timer_machine.access = TIMER_SLOW_ACCESS;
    slow_still_clock = timer_find();
    slow_still_nanoseconds = timer_machine.nanoseconds;
    timer_open(0u);
    timer_machine.bits = TIMER_EIGHT_BITS;
    eight_bits = timer_find();
    timer_open(0u);
    timer_machine.hertz = TIMER_HERTZ * 100ull;
    hundredfold = timer_find();

    kernel_test_check(test, still_clock == TIMER_STATE(NO_REFERENCE), "a still clock read fast: no reference");
    kernel_test_check(test, still_nanoseconds < 500ull * TIMER_MILLISECOND,
                      "found within the reads of the clock, before the timer counted half a second");
    kernel_test_check(test, slow_still_clock == TIMER_STATE(RAN_AHEAD), "a still clock read slowly: ran ahead");
    kernel_test_check(test,
                      slow_still_nanoseconds >= 500ull * TIMER_MILLISECOND &&
                          slow_still_nanoseconds < 1000ull * TIMER_MILLISECOND,
                      "once the timer counted half a second");
    kernel_test_check(test, eight_bits == TIMER_STATE(RAN_AHEAD), "a timer decoded on 8 bits ran ahead");
    kernel_test_check(test, hundredfold == TIMER_STATE(RAN_AHEAD), "a timer a hundred times too fast ran ahead");
}

/**
 * @brief A stall waits the microseconds asked as the machine's time counts them, and says so: 1 µs,
 *        50 µs, and 100 µs across the wrap of a 24-bit timer and across 2^24 for a 32-bit one; and a
 *        timer that holds each count for two reads in a row, more than 10,000 times in all, still counts
 *        a 10 ms wait to its end.
 */
KERNEL_TEST(a_stall_waits_its_microseconds_on_the_timer)
{
    uint32_t one_waited;
    uint32_t fifty_waited;
    uint32_t wrap_waited;
    uint32_t wide_waited;
    uint32_t stutter_waited;
    uint64_t one;
    uint64_t fifty;
    uint64_t wrap;
    uint64_t wide;
    uint64_t stutter;

    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    one = timer_stall(1u, &one_waited);
    fifty = timer_stall(50u, &fifty_waited);
    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    (void) timer_find();
    timer_place(TIMER_LOW_BITS - 50u);
    wrap = timer_wait(100u, &wrap_waited);
    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    timer_machine.bits = TIMER_ALL_BITS;
    (void) timer_find();
    timer_place(TIMER_LOW_BITS - 50u);
    wide = timer_wait(100u, &wide_waited);
    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    (void) timer_find();
    timer_machine.stutters = true;
    timer_machine.held_nanoseconds = timer_machine.nanoseconds;
    stutter = timer_wait(10000u, &stutter_waited);

    kernel_test_check(test, one >= 1000u && one < 2000u, "1 µs lasts 1 µs to 2 µs");
    kernel_test_check(test, one_waited >= 1u && one_waited <= 2u, "and says so");
    kernel_test_check(test, fifty >= 50000u && fifty < 51000u, "50 µs lasts 50 µs to 51 µs");
    kernel_test_check(test, fifty_waited >= 50u && fifty_waited <= 51u, "and says so");
    kernel_test_check(test, wrap >= 100000u && wrap < 101000u, "100 µs across the wrap of 24 bits lasts 100 µs");
    kernel_test_check(test, wrap_waited >= 100u && wrap_waited <= 101u, "and says so");
    kernel_test_check(test, wide >= 100000u && wide < 101000u, "100 µs across 2^24 on 32 bits lasts 100 µs");
    kernel_test_check(test, wide_waited >= 100u && wide_waited <= 101u, "and says so");
    kernel_test_check(test, stutter >= 10000000ull && stutter_waited >= 10000u,
                      "a timer that holds each count twice counts 10 ms to the end");
    kernel_test_check(test, timer_found.state == TIMER_STATE(USABLE) && timer_machine.host_stalls == 0u,
                      "without derailing, nor leaning on the host's stall");
    kernel_test_measure(test, "fifty_waited", fifty_waited);
}

/**
 * @brief A read caught in an increment does not end a stall early, whether it is the first or the
 *        middle read of a triple, with reads of 100 ns, where the two others are equal, and of 1 µs,
 *        where they are not; a timer that is not usable is not waited on, and no port is read.
 */
KERNEL_TEST(a_stall_survives_a_bad_read)
{
    uint32_t first_glitch_waited;
    uint32_t glitch_waited;
    uint32_t unusable_waited;
    uint32_t slow_glitch_waited;
    uint64_t first_glitch;
    uint64_t glitch;
    uint64_t slow_glitch;
    uint32_t reads_before;

    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    (void) timer_find();
    timer_machine.out_of_order = timer_machine.reads + 4u;
    first_glitch = timer_wait(50u, &first_glitch_waited);
    timer_machine.out_of_order = timer_machine.reads + 5u;
    glitch = timer_wait(50u, &glitch_waited);
    timer_open(0u);
    (void) timer_find();
    timer_machine.out_of_order = timer_machine.reads + 5u;
    slow_glitch = timer_wait(50u, &slow_glitch_waited);
    timer_found.state = TIMER_STATE(STOPPED);
    reads_before = timer_machine.reads;
    unusable_waited =
        advanced_configuration_and_power_interface_power_management_timer_stall(&timer_host, &timer_found, 50u);

    kernel_test_check(test, first_glitch >= 50000u && first_glitch_waited >= 50u,
                      "a first read caught in an increment ends nothing early");
    kernel_test_check(test, glitch >= 50000u && glitch_waited >= 50u, "nor does a middle one");
    kernel_test_check(test, slow_glitch >= 50000u && slow_glitch_waited >= 50u,
                      "nor a middle one between two unequal reads, at 1 µs a read");
    kernel_test_check(test, timer_machine.host_stalls == 0u, "and neither leans on the host's stall");
    kernel_test_check(test, unusable_waited == 0u && timer_machine.reads == reads_before,
                      "a timer that is not usable is not waited on");
}

/**
 * @brief A timer that goes back during a stall derails: the stall still lasts what it asked, the host's
 *        own stall finishing it, and the timer is no longer waited on.
 */
KERNEL_TEST(a_timer_that_goes_back_during_a_stall_derails)
{
    uint32_t waited;
    uint32_t later_waited;
    uint64_t elapsed;
    uint32_t reads_before;
    uint8_t before;

    timer_open(0x00100000u);
    before = timer_find();
    timer_machine.goes_back_at = timer_machine.reads + 16u;
    timer_machine.back_by = 0x2000u;
    elapsed = timer_wait(50u, &waited);
    reads_before = timer_machine.reads;
    later_waited =
        advanced_configuration_and_power_interface_power_management_timer_stall(&timer_host, &timer_found, 50u);

    kernel_test_check(test, before == TIMER_STATE(USABLE), "usable before it goes back");
    kernel_test_check(test, timer_found.state == TIMER_STATE(DERAILED), "derailed once a stall sees it go back");
    kernel_test_check(test, elapsed >= 50000u && waited == 50u, "the stall still lasts 50 µs, and says so");
    kernel_test_check(test,
                      timer_machine.host_stalls == 1u && timer_machine.host_microseconds > 0u &&
                          timer_machine.host_microseconds < 50u,
                      "the host's own stall finishing what the timer left");
    kernel_test_check(test, later_waited == 0u && timer_machine.reads == reads_before,
                      "and a later stall reads the timer no more");
    kernel_test_measure(test, "waited_us", waited);
}

/**
 * @brief A timer that stops during a stall derails once it has read the same count
 *        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_READ_CHECKS times: the stall still
 *        lasts what it asked, and a later stall pays no such wait again.
 */
KERNEL_TEST(a_timer_that_stops_during_a_stall_derails)
{
    uint32_t waited;
    uint32_t later_waited;
    uint64_t elapsed;
    uint64_t later;
    uint32_t stall_reads;

    timer_open(0u);
    (void) timer_find();
    timer_machine.stops_after = timer_machine.reads + 10u;
    elapsed = timer_wait(200u, &waited);
    stall_reads = timer_found.stall_reads;
    later = timer_wait(50u, &later_waited);

    kernel_test_check(test, timer_found.state == TIMER_STATE(DERAILED), "derailed once it stood still");
    kernel_test_check(test, elapsed >= 200000u && waited == 200u, "the stall still lasts 200 µs, and says so");
    kernel_test_check(
        test, stall_reads <= 3u * (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_READ_CHECKS + 10u),
        "after a bounded number of reads");
    kernel_test_check(test, later_waited == 0u && later == 0u, "and a later stall pays no such wait again");
    kernel_test_measure(test, "stall_reads", stall_reads);
}

/**
 * @brief Each state has the name the boot's telemetry prints, and a value past them is unknown.
 */
KERNEL_TEST(each_timer_state_has_its_name)
{
    const char *const names[] = {"absent",    "unreachable", "stopped", "inconsistent", "no-reference",
                                 "ran-ahead", "wrong-rate",  "usable",  "derailed"};
    bool every_name_held = true;

    for (uint8_t state = 0u; state < sizeof(names) / sizeof(names[0]); ++state)
        every_name_held =
            every_name_held &&
            timer_name_is(advanced_configuration_and_power_interface_power_management_timer_state_name(state),
                          names[state]);

    kernel_test_check(test, every_name_held,
                      "absent, unreachable, stopped, inconsistent, no-reference, ran-ahead, wrong-rate, usable, "
                      "derailed");
    kernel_test_check(
        test,
        timer_name_is(advanced_configuration_and_power_interface_power_management_timer_state_name(9u), "unknown"),
        "and 9 is unknown");
}
