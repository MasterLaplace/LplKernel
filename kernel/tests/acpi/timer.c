#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_power_management_timer, KERNEL_TEST_STAGE_INITIALIZATION);

#define TIMER_PORT            0x608u   /**< Where the made-up platform's timer lies, as on QEMU's q35. */
#define TIMER_HERTZ           3579545u /**< What a timer counts per second, written out apart from the code. */
#define TIMER_TICK            100000u  /**< A step of the made-up host's clock, 10 ms in units of 100 ns. */
#define TIMER_ACCESS          1000u    /**< Nanoseconds each read of the made-up host costs. */
#define TIMER_FASTEST_ACCESS  50u    /**< Nanoseconds a read costs where 30 ms take more samples than the poll bound. */
#define TIMER_FAST_ACCESS     100u   /**< Nanoseconds a read costs where a wait's precision is checked. */
#define TIMER_SLOW_ACCESS     10000u /**< Nanoseconds a read costs on a slow bus, 40 µs a poll. */
#define TIMER_MATRIX_ACCESS   4000u  /**< Nanoseconds a read costs where many timers are found in a row. */
#define TIMER_MILLISECOND     1000000u    /**< A millisecond of the made-up machine, in nanoseconds. */
#define TIMER_LOW_BITS        0x00FFFFFFu /**< The 24 bits every such timer runs. */
#define TIMER_ALL_BITS        0xFFFFFFFFu /**< A timer of 32 bits, TMR_VAL_EXT set. */
#define TIMER_EIGHT_BITS      0x000000FFu /**< A port that decodes only 8 bits of the count. */
#define TIMER_OUT_OF_ORDER    0x00400000u /**< What a read caught in an increment adds to the count. */
#define TIMER_NO_EVENT        0u          /**< A read or an edge number that names none. */
#define TIMER_STUTTER         6u          /**< Port reads a stuttering timer holds each count for: two triples. */
#define TIMER_WRAP_FIRST      20u /**< Counts before the wrap a timer starts at, fewest: it wraps in the first checks. */
#define TIMER_WRAP_LAST       60u /**< Most: two checks of reads later, so that one of them sees the wrap inside it. */
#define TIMER_IDLE_LONGEST    10000u /**< Microseconds of the made-up host's tick, the longest one of its idles lasts. */
#define TIMER_INTERRUPT       1000u  /**< Nanoseconds an idle lasts that an interrupt other than the tick ends. */
#define TIMER_UNIDLED         11250u   /**< The longest sleep that never idles: a tick and an eighth. */
#define TIMER_REFERENCE_HERTZ 1193182u /**< What the made-up reference counts per second, as PIT channel 2 does. */
#define TIMER_REFERENCE_RANGE 0xFFFFu  /**< Counts the made-up reference holds, 54.9 ms, as PIT channel 2 does. */
#define TIMER_REFERENCE_BACK  1000u    /**< Counts a reference that goes back reads less. */
#define TIMER_SHORT_PAUSE     60000u   /**< Nanoseconds of a pause that a sample's spread of 75 µs still holds. */
#define TIMER_WIDE_PAUSE      100000u  /**< Nanoseconds of a pause just past what a sample's spread of 75 µs holds. */
#define TIMER_STATE(name)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_##name
#define TIMER_REFERENCE_BACK_READ                                                                                      \
    3000u /**< The read from which a reference that goes back reads less, about 3 ms into a wait of 1 µs reads. */
#define TIMER_REFERENCE_DERAIL_STALL 10000u /**< Microseconds of a stall on a reference that derails during it. */
#define TIMER_REFERENCE_PAUSE_AT     10u    /**< Milliseconds into a stall at which the machine pauses. */
#define TIMER_REFERENCE_PAUSE        60u    /**< Milliseconds of that pause, past the 54.9 ms the reference holds. */
#define REFERENCE_STATE(name)        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_REFERENCE_WAITS_##name

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
    uint32_t second_late_edge;  /**< Another step of the clock seen late, or TIMER_NO_EVENT. */
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
    uint32_t idle_nanoseconds;  /**< How long an idle lasts; 0 for one that the next step of the clock ends. */
    uint32_t idles;             /**< Calls to the host's idle. */
    uint32_t host_sleeps;       /**< Calls to the host's own sleep. */
    uint32_t host_slept;        /**< Microseconds the host's own sleep waited. */
    uint32_t reference_hertz;   /**< What its reference counts per second; 0 for a reference that is not there. */
    uint32_t reference_range;   /**< Counts the reference holds: reading more is spent. */
    bool reference_still;       /**< The reference holds still at 0. */
    uint32_t reference_back_at; /**< The read of the reference from which it reads TIMER_REFERENCE_BACK less. */
    uint64_t reference_started; /**< The time its reference last started. */
    uint32_t reference_starts;  /**< Starts of the reference. */
    uint32_t reference_reads;   /**< Reads of the reference. */
    uint64_t pause_from;        /**< The time from which a read of the reference pauses the machine first. */
    uint64_t pause_again_from;  /**< The time from which one pauses it a second time, or 0 for no second pause. */
    uint32_t pause_nanoseconds; /**< How long a pause lasts; 0 for none. */
    uint32_t pauses;            /**< Pauses made. */
} TimerMachine_t;

static TimerMachine_t timer_machine;
static AdvancedConfigurationAndPowerInterfaceFixedDescription_t timer_fadt;
static AdvancedConfigurationAndPowerInterfacePowerManagementTimer_t timer_found;
static AdvancedConfigurationAndPowerInterfaceReferenceWaits_t timer_reference_waits;

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
 * @brief The made-up host's clock, in units of 100 ns, stepping by whole ticks, two of which it may
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
    if (((machine->late_edge != TIMER_NO_EVENT && edge == machine->late_edge) ||
         (machine->second_late_edge != TIMER_NO_EVENT && edge == machine->second_late_edge)) &&
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

/**
 * @brief The made-up host's own sleep: it waits exactly what it is asked, and counts it.
 */
static uint32_t timer_host_sleep(void *context, uint32_t microseconds)
{
    TimerMachine_t *machine = context;

    machine->nanoseconds += (uint64_t) microseconds * 1000u;
    ++machine->host_sleeps;
    machine->host_slept += microseconds;
    return microseconds;
}

/**
 * @brief The made-up host's idle: time runs to the next step of its clock, as a halt the next tick ends,
 *        or for idle_nanoseconds, as one that another interrupt ends.
 */
static void timer_idle(void *context)
{
    TimerMachine_t *machine = context;
    const uint64_t tick = (uint64_t) machine->tick * 100u;

    ++machine->idles;
    if (machine->idle_nanoseconds != 0u || tick == 0u)
        machine->nanoseconds += machine->idle_nanoseconds;
    else
        machine->nanoseconds = (machine->nanoseconds / tick + 1u) * tick;
}

/**
 * @brief The made-up host's reference starts counting reference_hertz from now; it gives that rate.
 */
static uint32_t timer_reference_start(void *context)
{
    TimerMachine_t *machine = context;

    machine->nanoseconds += machine->access;
    machine->reference_started = machine->nanoseconds;
    ++machine->reference_starts;
    return machine->reference_hertz;
}

/**
 * @brief Whether the read of the reference at the machine's time now pauses the machine first.
 */
static bool timer_pause_due(const TimerMachine_t *machine)
{
    if (machine->pause_nanoseconds == 0u)
        return false;
    if (machine->pauses == 0u)
        return machine->nanoseconds >= machine->pause_from;
    return machine->pauses == 1u && machine->pause_again_from != 0u &&
           machine->nanoseconds >= machine->pause_again_from;
}

/**
 * @brief The made-up host's reference: what it counted since it started, read after the pause that is
 *        due, if any, and less TIMER_REFERENCE_BACK from reference_back_at on; spent once it counted its
 *        range.
 */
static uint32_t timer_reference_read(void *context)
{
    TimerMachine_t *machine = context;
    uint64_t counted;

    machine->nanoseconds += machine->access;
    ++machine->reference_reads;
    if (timer_pause_due(machine))
    {
        machine->nanoseconds += machine->pause_nanoseconds;
        ++machine->pauses;
    }
    counted = ((machine->nanoseconds - machine->reference_started) * machine->reference_hertz) / 1000000000ull;
    if (machine->reference_still)
        return 0u;
    if (counted >= machine->reference_range)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_REFERENCE_SPENT;
    if (machine->reference_back_at != TIMER_NO_EVENT && machine->reference_reads >= machine->reference_back_at)
        counted -= TIMER_REFERENCE_BACK;
    return (uint32_t) counted;
}

static const AdvancedConfigurationAndPowerInterfaceHost_t timer_host = {
    .context = &timer_machine,
    .port_read = timer_port_read,
    .sleep_microseconds = timer_host_sleep,
    .stall_microseconds = timer_host_stall,
    .clock_100ns = timer_clock_100ns,
    .reference_start = timer_reference_start,
    .reference_read = timer_reference_read,
};

static const AdvancedConfigurationAndPowerInterfaceHost_t timer_host_without_reference = {
    .context = &timer_machine,
    .port_read = timer_port_read,
    .sleep_microseconds = timer_host_sleep,
    .stall_microseconds = timer_host_stall,
    .clock_100ns = timer_clock_100ns,
};

/**
 * @brief Resets the made-up machine to a 24-bit timer counting 3.579545 MHz from @p start, a clock of
 *        10 ms ticks, a reference that counts as PIT channel 2 does and reads of 1 µs, and the FADT to
 *        one that names the timer's port.
 */
static void timer_open(uint32_t start)
{
    memset(&timer_machine, 0, sizeof(timer_machine));
    timer_machine.access = TIMER_ACCESS;
    timer_machine.hertz = TIMER_HERTZ;
    timer_machine.start = start;
    timer_machine.bits = TIMER_LOW_BITS;
    timer_machine.tick = TIMER_TICK;
    timer_machine.reference_hertz = TIMER_REFERENCE_HERTZ;
    timer_machine.reference_range = TIMER_REFERENCE_RANGE;

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
 * @brief Sleeps @p microseconds on the timer last found, idling through timer_idle, and gives the
 *        nanoseconds that passed on the machine meanwhile.
 */
static uint64_t timer_sleep(uint32_t microseconds, uint32_t *out_waited)
{
    const uint64_t before = timer_machine.nanoseconds;

    *out_waited = advanced_configuration_and_power_interface_power_management_timer_sleep(
        &timer_host, &timer_found, microseconds, TIMER_IDLE_LONGEST, timer_idle);
    return timer_machine.nanoseconds - before;
}

/**
 * @brief Moves the made-up machine's time to @p nanoseconds past the next step of its clock.
 */
static void timer_after_a_step(uint64_t nanoseconds)
{
    const uint64_t tick = (uint64_t) timer_machine.tick * 100u;

    timer_machine.nanoseconds = (timer_machine.nanoseconds / tick + 1u) * tick + nanoseconds;
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
 * @brief Resets the made-up machine as timer_open does, and the waits made on its reference to none.
 */
static void timer_open_reference(void)
{
    timer_open(0u);
    memset(&timer_reference_waits, 0, sizeof(timer_reference_waits));
}

/**
 * @brief Busy-waits @p microseconds on the made-up machine's reference through @p host, and gives the
 *        nanoseconds that passed on the machine meanwhile.
 */
static uint64_t timer_reference_stall(const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t microseconds,
                                      uint32_t *out_waited)
{
    const uint64_t before = timer_machine.nanoseconds;

    *out_waited =
        advanced_configuration_and_power_interface_reference_stall(host, &timer_reference_waits, microseconds);
    return timer_machine.nanoseconds - before;
}

/**
 * @brief Sleeps @p microseconds on the made-up machine's reference, idling through timer_idle, and gives the
 *        nanoseconds that passed on the machine meanwhile.
 */
static uint64_t timer_reference_sleep(uint32_t microseconds, uint32_t *out_waited)
{
    const uint64_t before = timer_machine.nanoseconds;

    *out_waited = advanced_configuration_and_power_interface_reference_sleep(
        &timer_host, &timer_reference_waits, microseconds, TIMER_IDLE_LONGEST, timer_idle);
    return timer_machine.nanoseconds - before;
}

/**
 * @brief Busy-waits TIMER_REFERENCE_DERAIL_STALL on the made-up machine's reference as it is set, and checks
 *        that it derails there, that the host's own stall waits what it left, which @p whole says is all of
 *        it, and that a later stall waits nothing, reads nothing and calls no stall of the host.
 */
static void timer_reference_derails(KernelTest_t *test, bool whole, const char *claim)
{
    uint32_t waited;
    uint32_t later_waited;

    (void) timer_reference_stall(&timer_host, TIMER_REFERENCE_DERAIL_STALL, &waited);

    const uint32_t reads = timer_machine.reference_reads;
    const uint32_t starts = timer_machine.reference_starts;
    const uint64_t later = timer_reference_stall(&timer_host, TIMER_REFERENCE_DERAIL_STALL, &later_waited);

    kernel_test_check(test,
                      timer_reference_waits.state == REFERENCE_STATE(DERAILED) && timer_machine.host_stalls == 1u &&
                          waited >= TIMER_REFERENCE_DERAIL_STALL &&
                          (timer_machine.host_microseconds == TIMER_REFERENCE_DERAIL_STALL) == whole,
                      claim);
    kernel_test_check(test,
                      later == 0u && later_waited == 0u && timer_machine.reference_reads == reads &&
                          timer_machine.reference_starts == starts && timer_machine.host_stalls == 1u,
                      "and a later stall waits nothing, neither reading nor starting it, nor calling the host's stall");
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
 *        among the reads of the checks; the checks count their reads, and last the 30 ms the reference
 *        is counted over, 30 ms of the clock.
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
    kernel_test_check(test, low_microseconds == 30000u, "and last 30 ms of the clock");
    kernel_test_check(test, wide_state == TIMER_STATE(USABLE), "a 32-bit timer that crosses 2^24 is usable");
    kernel_test_measure(test, "rate_per_mille", low_rate);
    kernel_test_measure(test, "check_reads", low_reads);
    kernel_test_measure(test, "check_us", low_microseconds);
}

/**
 * @brief Steps of the clock seen late spoil no measurement, since the rate is no longer measured on the
 *        clock: the steps at 10 ms and 100 ms seen 5 ms late, which began the two measurements made on it
 *        and had an exact timer refused then, and a step seen 1 ms late, leave it usable at its rate; a
 *        timer 6 % fast has the wrong rate after one measurement, 30 ms of the clock.
 */
KERNEL_TEST(a_tick_seen_late_spoils_no_measurement)
{
    uint8_t one_late;
    uint8_t both_late;
    uint32_t both_late_rate;
    uint8_t fast;
    uint32_t fast_rate;
    uint32_t fast_microseconds;
    uint32_t fast_starts;

    timer_open(0u);
    timer_machine.late_edge = 1u;
    timer_machine.late_by = TIMER_MILLISECOND;
    one_late = timer_find();
    timer_open(0u);
    timer_machine.late_edge = 1u;
    timer_machine.second_late_edge = 10u;
    timer_machine.late_by = 5u * TIMER_MILLISECOND;
    both_late = timer_find();
    both_late_rate = timer_found.rate_per_mille;
    timer_open(0u);
    timer_machine.hertz = TIMER_HERTZ * 106ull / 100u;
    fast = timer_find();
    fast_rate = timer_found.rate_per_mille;
    fast_microseconds = timer_found.check_microseconds;
    fast_starts = timer_machine.reference_starts;

    kernel_test_check(test, one_late == TIMER_STATE(USABLE), "a tick 1 ms late leaves the timer usable");
    kernel_test_check(test, both_late == TIMER_STATE(USABLE) && both_late_rate >= 999u && both_late_rate <= 1001u,
                      "and so do the ticks that began both measurements 5 ms late, at its rate");
    kernel_test_check(test, fast == TIMER_STATE(WRONG_RATE) && fast_rate >= 1059u && fast_rate <= 1061u,
                      "a timer 6 % fast has the wrong rate");
    kernel_test_check(test, fast_microseconds == 30000u && fast_starts == 1u,
                      "after one measurement, 30 ms of the clock");
    kernel_test_measure(test, "both_late_rate", both_late_rate);
}

/**
 * @brief A pause of the processor across a read of the reference, of 100 µs, 1 ms or 20 ms, at the
 *        measurement's first sample, in its middle or at its last, moves no measurement: the sample it
 *        spreads past 75 µs is taken again, and the rate stays a thousand thousandths within a read.
 *        A pause of 60 µs at the first sample, which its spread still holds, moves the rate by half of
 *        it, since a sample's count is taken halfway between its two reads: 30 µs over 30 ms, a
 *        thousandth.
 */
KERNEL_TEST(a_pause_of_the_processor_moves_no_measurement)
{
    const uint64_t pauses_from[] = {0u, 15u * TIMER_MILLISECOND, 30u * TIMER_MILLISECOND};
    const uint32_t pauses_last[] = {TIMER_WIDE_PAUSE, TIMER_MILLISECOND, 20u * TIMER_MILLISECOND};
    uint32_t paused = 0u;
    uint32_t moved = 0u;
    uint32_t lowest_rate = UINT32_MAX;
    uint32_t highest_rate = 0u;

    for (uint32_t from = 0u; from < sizeof(pauses_from) / sizeof(pauses_from[0]); ++from)
        for (uint32_t last = 0u; last < sizeof(pauses_last) / sizeof(pauses_last[0]); ++last)
        {
            timer_open(0u);
            timer_machine.pause_from = pauses_from[from];
            timer_machine.pause_nanoseconds = pauses_last[last];

            const uint8_t state = timer_find();
            const uint32_t rate = timer_found.rate_per_mille;

            paused += timer_machine.pauses;
            moved += (state != TIMER_STATE(USABLE) || rate < 999u || rate > 1001u) ? 1u : 0u;
            lowest_rate = (rate < lowest_rate) ? rate : lowest_rate;
            highest_rate = (rate > highest_rate) ? rate : highest_rate;
        }

    timer_open(0u);
    timer_machine.pause_nanoseconds = TIMER_SHORT_PAUSE;

    const uint8_t short_state = timer_find();
    const uint32_t short_rate = timer_found.rate_per_mille;

    kernel_test_check(test, paused == 9u, "each of the nine measurements paused once");
    kernel_test_check(test, moved == 0u, "and each left the timer usable, at its rate within a read");
    kernel_test_check(test, short_state == TIMER_STATE(USABLE) && short_rate == 1001u && timer_machine.pauses == 1u,
                      "a pause of 60 µs the first sample holds moves the rate by half of it, a thousandth");
    kernel_test_measure(test, "lowest_rate", lowest_rate);
    kernel_test_measure(test, "highest_rate", highest_rate);
    kernel_test_measure(test, "short_rate", short_rate);
}

/**
 * @brief No pause of the processor across a read of the reference, of 1 ms, 5 ms or 20 ms at the first
 *        sample, in the middle or at the last, lets a timer 6 % fast or slow be found usable, and an exact
 *        one always is.
 */
KERNEL_TEST(no_pause_lets_a_timer_six_percent_off_through)
{
    const uint64_t pauses_from[] = {0u, 15u * TIMER_MILLISECOND, 30u * TIMER_MILLISECOND};
    const uint32_t pauses_last[] = {TIMER_MILLISECOND, 5u * TIMER_MILLISECOND, 20u * TIMER_MILLISECOND};
    const uint32_t per_hundred[] = {94u, 100u, 106u};
    uint32_t paused = 0u;
    uint32_t off_usable = 0u;
    uint32_t exact_refused = 0u;

    for (uint32_t from = 0u; from < sizeof(pauses_from) / sizeof(pauses_from[0]); ++from)
        for (uint32_t last = 0u; last < sizeof(pauses_last) / sizeof(pauses_last[0]); ++last)
            for (uint32_t rate = 0u; rate < sizeof(per_hundred) / sizeof(per_hundred[0]); ++rate)
            {
                timer_open(0u);
                timer_machine.access = TIMER_MATRIX_ACCESS;
                timer_machine.hertz = TIMER_HERTZ * (uint64_t) per_hundred[rate] / 100u;
                timer_machine.pause_from = pauses_from[from];
                timer_machine.pause_nanoseconds = pauses_last[last];

                const bool usable = timer_find() == TIMER_STATE(USABLE);

                paused += timer_machine.pauses;
                off_usable += (per_hundred[rate] != 100u && usable) ? 1u : 0u;
                exact_refused += (per_hundred[rate] == 100u && !usable) ? 1u : 0u;
            }

    kernel_test_check(test, paused == 27u, "each of the 27 measurements paused once");
    kernel_test_check(test, off_usable == 0u, "no timer 6 % off is found usable");
    kernel_test_check(test, exact_refused == 0u, "and every exact one is");
    kernel_test_measure(test, "off_usable", off_usable);
}

/**
 * @brief A pause longer than the reference holds, 60 ms, spoils the measurement it falls in, and the
 *        timer is measured again, usable at its rate; a second such pause, in the second measurement,
 *        leaves its rate unchecked, as does a host whose reference is not there, or that has none.
 */
KERNEL_TEST(a_reference_that_runs_out_is_started_once_more)
{
    uint8_t once;
    uint32_t once_rate;
    uint32_t once_starts;
    uint8_t twice;
    uint32_t twice_starts;
    uint8_t not_there;
    uint32_t not_there_reads;
    uint8_t none;

    timer_open(0u);
    timer_machine.pause_from = 10u * TIMER_MILLISECOND;
    timer_machine.pause_nanoseconds = 60u * TIMER_MILLISECOND;
    once = timer_find();
    once_rate = timer_found.rate_per_mille;
    once_starts = timer_machine.reference_starts;
    timer_open(0u);
    timer_machine.pause_from = 10u * TIMER_MILLISECOND;
    timer_machine.pause_again_from = 80u * TIMER_MILLISECOND;
    timer_machine.pause_nanoseconds = 60u * TIMER_MILLISECOND;
    twice = timer_find();
    twice_starts = timer_machine.reference_starts;
    timer_open(0u);
    timer_machine.reference_hertz = 0u;
    not_there = timer_find();
    not_there_reads = timer_machine.reference_reads;
    timer_open(0u);
    advanced_configuration_and_power_interface_find_power_management_timer(&timer_host_without_reference, &timer_fadt,
                                                                           &timer_found);
    none = timer_found.state;

    kernel_test_check(test, once == TIMER_STATE(USABLE) && once_rate >= 999u && once_rate <= 1001u,
                      "a reference that runs out once leaves the timer usable, at its rate");
    kernel_test_check(test, once_starts == 2u, "measured a second time");
    kernel_test_check(test, twice == TIMER_STATE(NO_REFERENCE) && twice_starts == 2u,
                      "one that runs out in both measurements leaves its rate unchecked");
    kernel_test_check(test, not_there == TIMER_STATE(NO_REFERENCE) && not_there_reads == 0u,
                      "and so does a reference that is not there, unread");
    kernel_test_check(test, none == TIMER_STATE(NO_REFERENCE), "or a host that has none");
}

/**
 * @brief A reference that goes back, by 1,000 counts from its thousandth read on, 7 ms into the measurement,
 *        leaves the rate unchecked.
 */
KERNEL_TEST(a_reference_that_goes_back_leaves_the_rate_unchecked)
{
    timer_open(0u);
    timer_machine.reference_back_at = 1000u;

    const uint8_t state = timer_find();

    kernel_test_check(test, state == TIMER_STATE(NO_REFERENCE) && timer_machine.reference_reads >= 1000u,
                      "a reference that goes back: no reference");
}

/**
 * @brief With one step of the clock seen 1 or 2 ms late, at 10, 90, 100 or 180 ms, where the measurements
 *        made on the clock began and ended, no timer 8 % fast or slow is found usable, and an exact one
 *        always is.
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
 * @brief A clock that stands still leaves the timer usable, since its rate is not measured on the clock;
 *        a reference that stands still leaves the rate unchecked however it is read: read fast, once it
 *        was read ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_REFERENCE_POLLS times,
 *        while one that moves, read so fast that its 30 ms take more samples than that, is measured; read
 *        slowly, once the timer counted half a second. A timer that counts half a second while the
 *        reference moves ran ahead, as one decoded on 8 bits or a hundred times too fast does; the one a
 *        hundred times too fast is found as soon as it counted that half second, 5 ms of the machine's
 *        time, since a measurement whose first sample ran ahead goes no further.
 */
KERNEL_TEST(a_still_reference_reads_as_no_reference_however_it_is_read)
{
    uint8_t still_clock;
    uint8_t still_reference;
    uint64_t still_nanoseconds;
    uint8_t fast_moving;
    uint32_t fast_moving_reads;
    uint8_t slow_still_reference;
    uint64_t slow_still_nanoseconds;
    uint8_t eight_bits;
    uint8_t hundredfold;
    uint64_t hundredfold_nanoseconds;

    timer_open(0u);
    timer_machine.tick = 0u;
    still_clock = timer_find();
    timer_open(0u);
    timer_machine.reference_still = true;
    still_reference = timer_find();
    still_nanoseconds = timer_machine.nanoseconds;
    timer_open(0u);
    timer_machine.access = TIMER_FASTEST_ACCESS;
    fast_moving = timer_find();
    fast_moving_reads = timer_found.check_reads;
    timer_open(0u);
    timer_machine.reference_still = true;
    timer_machine.access = TIMER_SLOW_ACCESS;
    slow_still_reference = timer_find();
    slow_still_nanoseconds = timer_machine.nanoseconds;
    timer_open(0u);
    timer_machine.bits = TIMER_EIGHT_BITS;
    eight_bits = timer_find();
    timer_open(0u);
    timer_machine.hertz = TIMER_HERTZ * 100ull;
    hundredfold = timer_find();
    hundredfold_nanoseconds = timer_machine.nanoseconds;

    kernel_test_check(test, still_clock == TIMER_STATE(USABLE), "a still clock leaves the timer usable");
    kernel_test_check(test, still_reference == TIMER_STATE(NO_REFERENCE), "a still reference read fast: no reference");
    kernel_test_check(test, still_nanoseconds < 500ull * TIMER_MILLISECOND,
                      "found within the reads of the reference, before the timer counted half a second");
    kernel_test_check(test,
                      fast_moving == TIMER_STATE(USABLE) &&
                          fast_moving_reads >
                              6u * ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_REFERENCE_POLLS,
                      "one that moves is measured, over more samples than that");
    kernel_test_check(test, slow_still_reference == TIMER_STATE(NO_REFERENCE),
                      "a still reference read slowly: no reference");
    kernel_test_check(test,
                      slow_still_nanoseconds >= 500ull * TIMER_MILLISECOND &&
                          slow_still_nanoseconds < 1000ull * TIMER_MILLISECOND,
                      "once the timer counted half a second");
    kernel_test_check(test, eight_bits == TIMER_STATE(RAN_AHEAD), "a timer decoded on 8 bits ran ahead");
    kernel_test_check(test, hundredfold == TIMER_STATE(RAN_AHEAD), "a timer a hundred times too fast ran ahead");
    kernel_test_check(test, hundredfold_nanoseconds < 6ull * TIMER_MILLISECOND,
                      "as soon as it counted half a second, 5 ms of it");
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
 *        middle read of a triple, with reads of 100 ns and of 1 µs, or a middle read between two equal
 *        ones while the timer holds its count; a timer that is not usable is not waited on, and no port is
 *        read.
 */
KERNEL_TEST(a_stall_survives_a_bad_read)
{
    uint32_t first_glitch_waited;
    uint32_t glitch_waited;
    uint32_t unusable_waited;
    uint32_t slow_glitch_waited;
    uint32_t held_glitch_waited;
    uint64_t first_glitch;
    uint64_t glitch;
    uint64_t slow_glitch;
    uint64_t held_glitch;
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
    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    (void) timer_find();
    timer_machine.stutters = true;
    timer_machine.held_nanoseconds = timer_machine.nanoseconds;
    timer_machine.out_of_order = timer_machine.reads + 5u;
    held_glitch = timer_wait(50u, &held_glitch_waited);
    timer_found.state = TIMER_STATE(STOPPED);
    reads_before = timer_machine.reads;
    unusable_waited =
        advanced_configuration_and_power_interface_power_management_timer_stall(&timer_host, &timer_found, 50u);

    kernel_test_check(test, first_glitch >= 50000u && first_glitch_waited >= 50u,
                      "a first read caught in an increment ends nothing early");
    kernel_test_check(test, glitch >= 50000u && glitch_waited >= 50u, "nor does a middle one");
    kernel_test_check(test, slow_glitch >= 50000u && slow_glitch_waited >= 50u,
                      "nor a middle one between two unequal reads, at 1 µs a read");
    kernel_test_check(test, held_glitch >= 50000u && held_glitch_waited >= 50u,
                      "nor a middle one between two equal reads, which Linux's test lets through");
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
 * @brief A sleep waits the microseconds asked as the machine's time counts them, and says so: 25 ms from
 *        1 ms past a step of the clock idle through the two steps before the last 6 ms and busy-wait
 *        those; 1 ms never idles.
 */
KERNEL_TEST(a_sleep_idles_through_whole_ticks_and_busy_waits_the_rest)
{
    uint32_t long_waited;
    uint32_t short_waited;
    uint32_t long_idles;
    uint64_t long_sleep;
    uint64_t short_sleep;

    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    (void) timer_find();
    timer_after_a_step(TIMER_MILLISECOND);
    long_sleep = timer_sleep(25000u, &long_waited);
    long_idles = timer_machine.idles;
    short_sleep = timer_sleep(1000u, &short_waited);

    kernel_test_check(test, long_sleep >= 25000000u && long_sleep < 25001000u, "25 ms lasts 25 ms to 25.001 ms");
    kernel_test_check(test, long_waited >= 25000u && long_waited <= 25001u, "and says so");
    kernel_test_check(test, long_idles == 2u, "idling through the two steps of the clock before its last 6 ms");
    kernel_test_check(test, short_sleep >= 1000000u && short_sleep < 1001000u, "1 ms lasts 1 ms to 1.001 ms");
    kernel_test_check(test, short_waited >= 1000u && short_waited <= 1001u, "and says so");
    kernel_test_check(test, timer_machine.idles == long_idles, "without idling");
    kernel_test_check(test,
                      timer_found.sleeps == 2u && timer_found.sleep_idles == 2u && timer_found.stalls == 0u &&
                          timer_found.stall_reads == 0u && timer_found.state == TIMER_STATE(USABLE),
                      "two sleeps and their idles are counted, no busy-wait, and the timer stays usable");
    kernel_test_check(test, timer_machine.host_sleeps == 0u && timer_machine.host_stalls == 0u,
                      "without leaning on the host's own waits");
    kernel_test_measure(test, "sleep_reads", timer_found.sleep_reads);
}

/**
 * @brief A sleep counts across the wrap of the 24-bit counter inside a halt: 25 ms from 20,000 counts
 *        before the wrap, 1 ms past a step of the clock, still last 25 ms to 25.001 ms, and the timer stays
 *        usable.
 */
KERNEL_TEST(a_sleep_counts_across_the_wrap_of_24_bits_inside_a_halt)
{
    uint32_t waited;
    uint64_t elapsed;

    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    (void) timer_find();
    timer_after_a_step(TIMER_MILLISECOND);
    timer_place(TIMER_LOW_BITS - 20000u);
    elapsed = timer_sleep(25000u, &waited);

    kernel_test_check(test, elapsed >= 25000000u && elapsed < 25001000u, "25 ms lasts 25 ms to 25.001 ms");
    kernel_test_check(test, waited >= 25000u && waited <= 25001u, "and says so");
    kernel_test_check(test, timer_machine.idles == 2u, "idling twice, the first time across the wrap");
    kernel_test_check(test, timer_found.state == TIMER_STATE(USABLE) && timer_machine.host_sleeps == 0u,
                      "the timer stays usable, and the host's own sleep is not called");
}

/**
 * @brief A sleep idles only while more than a tick and an eighth remain: 11,250 µs never idles, and
 *        11,251 µs idles once, from the step of the clock, 5 ms past it, and 1 µs before the next.
 */
KERNEL_TEST(a_sleep_idles_only_while_more_than_a_tick_and_an_eighth_remain)
{
    static const uint64_t phases[] = {0u, 5u * TIMER_MILLISECOND, 10u * TIMER_MILLISECOND - TIMER_ACCESS};
    uint32_t waited;
    bool short_never_idles = true;
    bool long_idles_once = true;

    timer_open(0u);
    (void) timer_find();
    for (uint32_t index = 0u; index < sizeof(phases) / sizeof(phases[0]); ++index)
    {
        uint32_t idles = timer_machine.idles;

        timer_after_a_step(phases[index]);
        (void) timer_sleep(TIMER_UNIDLED, &waited);
        short_never_idles = short_never_idles && timer_machine.idles == idles && waited >= TIMER_UNIDLED;
        idles = timer_machine.idles;
        timer_after_a_step(phases[index]);
        (void) timer_sleep(TIMER_UNIDLED + 1u, &waited);
        long_idles_once = long_idles_once && timer_machine.idles == idles + 1u && waited > TIMER_UNIDLED;
    }
    kernel_test_check(test, short_never_idles, "11,250 µs never idles");
    kernel_test_check(test, long_idles_once, "11,251 µs idles once");
}

/**
 * @brief A timer that stops while a sleep idles derails once the clock has stepped twice without it
 *        moving: the sleep still lasts what it asked, the host's own sleep finishing it, and a later
 *        sleep reads the timer no more.
 */
KERNEL_TEST(a_timer_that_stops_while_a_sleep_idles_derails)
{
    uint32_t waited;
    uint32_t later_waited;
    uint64_t elapsed;
    uint64_t later;
    uint32_t reads_before;

    timer_open(0u);
    (void) timer_find();
    timer_machine.stops_after = timer_machine.reads + 4u;
    elapsed = timer_sleep(100000u, &waited);
    reads_before = timer_machine.reads;
    later = timer_sleep(1000u, &later_waited);

    kernel_test_check(test, timer_found.state == TIMER_STATE(DERAILED), "derailed once it stood still");
    kernel_test_check(test, timer_machine.idles == 3u,
                      "after three idles: the one that saw it move last, then two steps of the clock");
    kernel_test_check(test, elapsed >= 100000000u && waited >= 100000u, "the sleep still lasts 100 ms, and says so");
    kernel_test_check(test,
                      timer_machine.host_sleeps == 1u && timer_machine.host_slept > 99000u &&
                          timer_machine.host_slept < 100000u,
                      "the host's own sleep finishing what the timer left");
    kernel_test_check(test, later_waited == 0u && later == 0u && timer_machine.reads == reads_before,
                      "and a later sleep reads the timer no more");
}

/**
 * @brief A timer that holds each count for two reads in a row, so that every other idle reads it unmoved
 *        across a step of the clock, still sleeps 100 ms to the end without derailing: only two steps in a
 *        row without a move derail it.
 */
KERNEL_TEST(a_timer_still_across_one_step_at_a_time_stays_usable)
{
    uint32_t waited;
    uint64_t elapsed;

    timer_open(0u);
    (void) timer_find();
    timer_machine.stutters = true;
    timer_machine.reads = 0u;
    timer_machine.held_nanoseconds = timer_machine.nanoseconds;
    elapsed = timer_sleep(100000u, &waited);

    kernel_test_check(test, timer_found.state == TIMER_STATE(USABLE) && timer_machine.host_sleeps == 0u,
                      "the timer stays usable, and the host's own sleep is not called");
    kernel_test_check(test, timer_machine.idles >= 8u, "through the idles of 100 ms");
    kernel_test_check(test, elapsed >= 100000000u && waited >= 100000u, "the sleep lasts 100 ms, and says so");
}

/**
 * @brief A timer that stops while interrupts other than the tick end each idle derails once the clock
 *        has stepped twice since it last moved, a whole tick of idles, and not at the first step.
 */
KERNEL_TEST(a_timer_that_stops_between_two_steps_derails_at_the_second)
{
    uint32_t waited;
    uint64_t elapsed;

    timer_open(0u);
    (void) timer_find();
    timer_after_a_step(0u);
    timer_machine.idle_nanoseconds = TIMER_INTERRUPT;
    timer_machine.stops_after = timer_machine.reads + 4u;
    elapsed = timer_sleep(100000u, &waited);

    kernel_test_check(test, timer_found.state == TIMER_STATE(DERAILED), "derailed once it stood still");
    kernel_test_check(test,
                      timer_machine.idles > 3000u &&
                          timer_machine.idles <
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_READ_CHECKS,
                      "after the second step of the clock, 20 ms of idles of 5 µs, before its read checks");
    kernel_test_check(test, elapsed >= 100000000u && waited >= 100000u && timer_machine.host_sleeps == 1u,
                      "the sleep still lasts 100 ms, the host's own sleep finishing it");
    kernel_test_measure(test, "idles", timer_machine.idles);
}

/**
 * @brief A timer that goes back while a sleep idles derails at the first read after the idle: the sleep
 *        still lasts what it asked, the host's own sleep finishing it.
 */
KERNEL_TEST(a_timer_that_goes_back_while_a_sleep_idles_derails)
{
    uint32_t waited;
    uint64_t elapsed;

    timer_open(0x00100000u);
    (void) timer_find();
    timer_machine.goes_back_at = timer_machine.reads + 4u;
    timer_machine.back_by = 0x00100000u;
    elapsed = timer_sleep(100000u, &waited);

    kernel_test_check(test, timer_found.state == TIMER_STATE(DERAILED), "derailed once it went back");
    kernel_test_check(test, timer_machine.idles == 1u, "at the first read after an idle");
    kernel_test_check(test, elapsed >= 100000000u && waited >= 100000u, "the sleep still lasts 100 ms, and says so");
    kernel_test_check(test, timer_machine.host_sleeps == 1u, "the host's own sleep finishing it");
}

/**
 * @brief A timer that stops while the clock stands still and interrupts end each idle derails once it
 *        has read the same count ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_READ_CHECKS
 *        times in a row, instead of idling for ever.
 */
KERNEL_TEST(a_timer_that_stops_while_the_clock_stands_still_derails_after_its_read_checks)
{
    uint32_t waited;
    uint64_t elapsed;

    timer_open(0u);
    (void) timer_find();
    timer_machine.tick = 0u;
    timer_machine.idle_nanoseconds = TIMER_INTERRUPT;
    timer_machine.stops_after = timer_machine.reads + 4u;
    elapsed = timer_sleep(100000u, &waited);

    kernel_test_check(test, timer_found.state == TIMER_STATE(DERAILED), "derailed once it stood still");
    kernel_test_check(
        test, timer_machine.idles == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_READ_CHECKS + 1u,
        "after the idle that saw it move last, then its read checks");
    kernel_test_check(test, elapsed >= 100000000u && waited >= 100000u && timer_machine.host_sleeps == 1u,
                      "the sleep still lasts 100 ms, the host's own sleep finishing it");
}

/**
 * @brief An idle that outlasts the whole sleep, as one that a tick taken late ends, ends the sleep at the
 *        read after it, without busy-waiting, and the sleep says it waited that long.
 */
KERNEL_TEST(an_idle_that_outlasts_a_sleep_ends_it)
{
    uint32_t waited;
    uint64_t elapsed;

    timer_open(0u);
    (void) timer_find();
    timer_machine.idle_nanoseconds = 30u * TIMER_MILLISECOND;
    elapsed = timer_sleep(20000u, &waited);

    kernel_test_check(test, timer_machine.idles == 1u && timer_found.sleep_reads == 6u,
                      "one idle, and one read of the timer on either side of it");
    kernel_test_check(test, elapsed >= 30000000u && elapsed < 30010000u && waited >= 30000u && waited <= 30010u,
                      "the sleep of 20 ms lasts the 30 ms of the idle, and says so");
    kernel_test_check(test, timer_found.state == TIMER_STATE(USABLE) && timer_machine.host_sleeps == 0u,
                      "the timer stays usable, and the host's own sleep is not called");
}

/**
 * @brief A sleep of nothing, and one on a timer that is not usable, wait nothing, read no port and do
 *        not idle.
 */
KERNEL_TEST(a_sleep_of_nothing_or_on_a_timer_that_is_not_usable_waits_nothing)
{
    uint32_t nothing_waited;
    uint32_t unusable_waited;
    uint64_t nothing;
    uint64_t unusable;
    uint32_t reads_before;

    timer_open(0u);
    (void) timer_find();
    reads_before = timer_machine.reads;
    nothing = timer_sleep(0u, &nothing_waited);
    timer_found.state = TIMER_STATE(STOPPED);
    unusable = timer_sleep(1000u, &unusable_waited);

    kernel_test_check(test, nothing == 0u && nothing_waited == 0u, "a sleep of nothing waits nothing");
    kernel_test_check(test, unusable == 0u && unusable_waited == 0u, "nor does one on a timer that is not usable");
    kernel_test_check(test,
                      timer_machine.reads == reads_before && timer_machine.idles == 0u &&
                          timer_machine.host_sleeps == 0u && timer_found.sleeps == 0u,
                      "and neither reads its port, idles, nor leans on the host's sleep");
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

/**
 * @brief A stall on the reference waits the microseconds asked as the machine's time counts them, and says
 *        so: 1 µs and 50 µs, each from a start of the reference, which stays usable, without the host's stall.
 */
KERNEL_TEST(a_stall_waits_its_microseconds_on_the_reference)
{
    uint32_t one_waited;
    uint32_t fifty_waited;
    uint64_t one;
    uint64_t fifty;

    timer_open_reference();
    timer_machine.access = TIMER_FAST_ACCESS;
    one = timer_reference_stall(&timer_host, 1u, &one_waited);
    fifty = timer_reference_stall(&timer_host, 50u, &fifty_waited);

    kernel_test_check(test, one >= 1000u && one < 2000u, "1 µs lasts 1 µs to 2 µs");
    kernel_test_check(test, one_waited >= 1u && one_waited <= 2u, "and says so");
    kernel_test_check(test, fifty >= 50000u && fifty < 51000u, "50 µs lasts 50 µs to 51 µs");
    kernel_test_check(test, fifty_waited >= 50u && fifty_waited <= 51u, "and says so");
    kernel_test_check(test,
                      timer_reference_waits.state == REFERENCE_STATE(USABLE) &&
                          timer_reference_waits.hertz == TIMER_REFERENCE_HERTZ && timer_reference_waits.stalls == 2u &&
                          timer_reference_waits.starts == 2u && timer_machine.reference_starts == 2u,
                      "each starts the reference once, at the rate it gives, and it stays usable");
    kernel_test_check(test, timer_machine.host_stalls == 0u && timer_machine.host_sleeps == 0u,
                      "without leaning on the host's own waits");
    kernel_test_measure(test, "fifty_waited", fifty_waited);
    kernel_test_measure(test, "stall_reads", timer_reference_waits.stall_reads);
}

/**
 * @brief A sleep on the reference waits the microseconds asked as the machine's time counts them, and says
 *        so: 100 ms from 1 ms past a step of the clock idle through the nine steps before its last 11 ms and
 *        busy-wait those, and start the reference again at the first read after each run of it counted 15 ms,
 *        at 30, 50, 70, 90 and 105 ms; 1 ms never idles.
 */
KERNEL_TEST(a_sleep_on_the_reference_idles_through_whole_ticks_and_starts_it_again_every_15_ms)
{
    uint32_t long_waited;
    uint32_t short_waited;
    uint32_t long_idles;
    uint64_t long_sleep;
    uint64_t short_sleep;

    timer_open_reference();
    timer_machine.access = TIMER_FAST_ACCESS;
    timer_after_a_step(TIMER_MILLISECOND);
    long_sleep = timer_reference_sleep(100000u, &long_waited);
    long_idles = timer_machine.idles;

    const uint32_t long_starts = timer_machine.reference_starts;

    short_sleep = timer_reference_sleep(1000u, &short_waited);

    kernel_test_check(test, long_sleep >= 100000000u && long_sleep < 100010000u, "100 ms lasts 100 ms to 100.01 ms");
    kernel_test_check(test, long_waited >= 100000u && long_waited <= 100010u, "and says so");
    kernel_test_check(test, long_idles == 9u, "idling through the nine steps of the clock before its last 11 ms");
    kernel_test_check(test, long_starts == 6u,
                      "starting the reference again after each run of it that counted 15 ms, five times");
    kernel_test_check(test, short_sleep >= 1000000u && short_sleep < 1001000u, "1 ms lasts 1 ms to 1.001 ms");
    kernel_test_check(test, short_waited >= 1000u && short_waited <= 1001u, "and says so");
    kernel_test_check(test, timer_machine.idles == long_idles, "without idling");
    kernel_test_check(test,
                      timer_reference_waits.sleeps == 2u && timer_reference_waits.sleep_idles == 9u &&
                          timer_reference_waits.stalls == 0u && timer_reference_waits.stall_reads == 0u &&
                          timer_reference_waits.starts == timer_machine.reference_starts &&
                          timer_reference_waits.state == REFERENCE_STATE(USABLE),
                      "two sleeps, their idles and their starts are counted, no busy-wait, and the reference stays "
                      "usable");
    kernel_test_check(test, timer_machine.host_sleeps == 0u && timer_machine.host_stalls == 0u,
                      "without leaning on the host's own waits");
    kernel_test_measure(test, "sleep_reads", timer_reference_waits.sleep_reads);
}

/**
 * @brief A read that finds the reference spent, the machine paused past its range, counts the 30 ms a
 *        reference holds at least, never more: a stall of 50 ms across a pause of 60 ms at its 10th ms lasts
 *        90 ms, says it waited 50 ms, and starts the reference again after the pause and 15 ms later.
 */
KERNEL_TEST(a_reference_found_spent_counts_the_30_ms_it_holds_at_least)
{
    uint32_t waited;
    uint64_t elapsed;

    timer_open_reference();
    timer_machine.access = TIMER_FAST_ACCESS;
    timer_machine.pause_from = (uint64_t) TIMER_REFERENCE_PAUSE_AT * TIMER_MILLISECOND;
    timer_machine.pause_nanoseconds = TIMER_REFERENCE_PAUSE * TIMER_MILLISECOND;
    elapsed = timer_reference_stall(&timer_host, 50000u, &waited);

    kernel_test_check(test, timer_machine.pauses == 1u && elapsed >= 90000000u && elapsed < 90010000u,
                      "a stall of 50 ms across a pause of 60 ms at its 10th ms lasts 90 ms to 90.01 ms");
    kernel_test_check(test, waited >= 50000u && waited <= 50010u, "and says it waited 50 ms");
    kernel_test_check(test,
                      timer_reference_waits.starts == 3u && timer_reference_waits.state == REFERENCE_STATE(USABLE) &&
                          timer_machine.host_stalls == 0u,
                      "starting the reference again after the pause and 15 ms later, without the host's stall");
}

/**
 * @brief A reference that reads spent at once after its start, as a port nothing answers does, one that goes
 *        back, and one that stands still for 10,000 reads derail: the host's own stall waits what each left,
 *        all of it for the first and the last, and a later stall does not wait on them again.
 */
KERNEL_TEST(a_reference_spent_at_its_start_going_back_or_standing_still_derails)
{
    timer_open_reference();
    timer_machine.reference_range = 0u;
    timer_reference_derails(test, true, "a reference spent at once after its start derails, and the host waits all");
    kernel_test_check(test, timer_machine.reference_starts == 1u && timer_machine.reference_reads == 1u,
                      "after one start and one read");

    timer_open_reference();
    timer_machine.reference_back_at = TIMER_REFERENCE_BACK_READ;
    timer_reference_derails(test, false, "one that goes back derails, and the host waits what it left");
    kernel_test_check(test, timer_machine.reference_reads == TIMER_REFERENCE_BACK_READ, "at the read that went back");

    timer_open_reference();
    timer_machine.reference_still = true;
    timer_reference_derails(test, true, "one that stands still derails, and the host waits all");
    kernel_test_check(test,
                      timer_machine.reference_reads ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_READ_CHECKS + 1u,
                      "after the read at its start and 10,000 that read it again");
}

/**
 * @brief A sleep on a reference that stands still while the clock steps twice derails at the second step it
 *        idles through, as one on the timer does, and the host's own sleep waits the whole: 10,000 idles do not.
 */
KERNEL_TEST(a_sleep_on_a_reference_that_stands_still_while_the_clock_steps_twice_derails)
{
    uint32_t waited;

    timer_open_reference();
    timer_machine.reference_still = true;
    (void) timer_reference_sleep(100000u, &waited);

    kernel_test_check(test,
                      timer_machine.idles == 2u && timer_reference_waits.sleep_idles == 2u &&
                          timer_reference_waits.state == REFERENCE_STATE(DERAILED),
                      "it derails at the second step of the clock it idles through");
    kernel_test_check(test, timer_machine.host_sleeps == 1u && timer_machine.host_slept == 100000u && waited >= 100000u,
                      "and the host's sleep waits the whole 100 ms");
}

/**
 * @brief A host without a reference, and one whose reference gives no rate, have none: the first stall
 *        finds it absent, reading nothing, and the host's own stall waits it; a later one waits nothing and
 *        calls no stall of the host; a sleep of nothing starts no reference.
 */
KERNEL_TEST(a_host_without_a_reference_waits_on_its_own_and_tries_it_once)
{
    uint32_t first_waited;
    uint32_t later_waited;
    uint32_t nothing_waited;
    uint64_t later;
    uint64_t nothing;

    timer_open_reference();
    (void) timer_reference_stall(&timer_host_without_reference, 50u, &first_waited);
    later = timer_reference_stall(&timer_host_without_reference, 50u, &later_waited);

    kernel_test_check(test,
                      timer_reference_waits.state == REFERENCE_STATE(ABSENT) && first_waited == 50u &&
                          timer_machine.host_stalls == 1u && timer_machine.host_microseconds == 50u,
                      "the first stall finds no reference, and the host's stall waits it");
    kernel_test_check(test, later == 0u && later_waited == 0u && timer_machine.host_stalls == 1u,
                      "a later one waits nothing, and does not call the host's stall again");

    timer_open_reference();
    timer_machine.reference_hertz = 0u;
    (void) timer_reference_stall(&timer_host, 50u, &first_waited);

    kernel_test_check(test,
                      timer_reference_waits.state == REFERENCE_STATE(ABSENT) && timer_machine.reference_starts == 1u &&
                          timer_machine.reference_reads == 0u && timer_machine.host_microseconds == 50u,
                      "a reference whose start gives no rate is absent, read never, and the host waits");

    timer_open_reference();
    nothing = timer_reference_sleep(0u, &nothing_waited);

    kernel_test_check(test,
                      nothing == 0u && nothing_waited == 0u && timer_machine.reference_starts == 0u &&
                          timer_reference_waits.state == REFERENCE_STATE(UNTRIED),
                      "a sleep of nothing waits nothing and starts no reference");
}

/**
 * @brief Each state of the reference waits has the name the boot's telemetry prints, and a value past them is
 *        unknown.
 */
KERNEL_TEST(each_reference_waits_state_has_its_name)
{
    const char *const names[] = {"untried", "absent", "usable", "derailed"};
    bool every_name_held = true;

    for (uint8_t state = 0u; state < sizeof(names) / sizeof(names[0]); ++state)
        every_name_held =
            every_name_held &&
            timer_name_is(advanced_configuration_and_power_interface_reference_waits_state_name(state), names[state]);

    kernel_test_check(test, every_name_held, "untried, absent, usable, derailed");
    kernel_test_check(
        test, timer_name_is(advanced_configuration_and_power_interface_reference_waits_state_name(4u), "unknown"),
        "and 4 is unknown");
}
