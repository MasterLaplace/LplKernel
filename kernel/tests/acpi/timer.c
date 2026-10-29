#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_power_management_timer, KERNEL_TEST_STAGE_INITIALIZATION);

#define TIMER_PORT         0x608u      /**< Where the made-up platform's timer lies, as on QEMU's q35. */
#define TIMER_HERTZ        3579545u    /**< What a timer counts per second, written out apart from the code. */
#define TIMER_TICK         100000u     /**< A step of the made-up host's clock, 10 ms in units of 100 ns. */
#define TIMER_ACCESS       1000u       /**< Nanoseconds each read of the made-up host costs. */
#define TIMER_FAST_ACCESS  100u        /**< Nanoseconds a read costs where a wait's precision is checked. */
#define TIMER_SLOW_ACCESS  100000u     /**< Nanoseconds a read costs where a bound of time is checked. */
#define TIMER_LOW_BITS     0x00FFFFFFu /**< The 24 bits every such timer runs. */
#define TIMER_ALL_BITS     0xFFFFFFFFu /**< A timer of 32 bits, TMR_VAL_EXT set. */
#define TIMER_OUT_OF_ORDER 0x00400000u /**< What a read caught in an increment adds to the count. */
#define TIMER_NO_EVENT     0u          /**< A read number that names no read. */
#define TIMER_STATE(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_##name

/**
 * @brief A made-up machine whose time moves with each read made of it, its timer counting at its own
 *        rate and its clock stepping by whole ticks, as the kernel's does.
 */
typedef struct TimerMachine {
    uint64_t nanoseconds;   /**< Time on the machine. */
    uint32_t access;        /**< Nanoseconds each read of a port or of the clock adds. */
    uint64_t hertz;         /**< What its timer counts per second. */
    uint32_t start;         /**< The timer's count at time 0. */
    uint32_t bits;          /**< The bits of the count the port gives. */
    uint32_t tick;          /**< The step of the clock in units of 100 ns; 0 for a clock that stands still. */
    uint32_t reads;         /**< Reads of the timer's port. */
    uint32_t out_of_order;  /**< The read that gives a count caught in an increment, or TIMER_NO_EVENT. */
    uint32_t stops_after;   /**< The read from which the timer holds still, or TIMER_NO_EVENT. */
    uint32_t stopped_count; /**< What it holds then. */
    bool counts_down;       /**< The timer goes back instead of forward. */
} TimerMachine_t;

static TimerMachine_t timer_machine;
static AdvancedConfigurationAndPowerInterfaceFixedDescription_t timer_fadt;
static AdvancedConfigurationAndPowerInterfacePowerManagementTimer_t timer_found;

/**
 * @brief What the made-up timer counts at the machine's time.
 */
static uint32_t timer_count(const TimerMachine_t *machine)
{
    const uint32_t counted = (uint32_t) ((machine->nanoseconds * machine->hertz) / 1000000000ull);

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
    count = timer_count(machine);
    if (machine->stops_after != TIMER_NO_EVENT && machine->reads + 1u == machine->stops_after)
        machine->stopped_count = count;
    if (machine->reads == machine->out_of_order)
        count += TIMER_OUT_OF_ORDER;
    return count & machine->bits;
}

/**
 * @brief The made-up host's clock, in units of 100 ns, stepping by whole ticks.
 */
static uint64_t timer_clock_100ns(void *context)
{
    TimerMachine_t *machine = context;

    machine->nanoseconds += machine->access;
    if (machine->tick == 0u)
        return 0u;
    return ((machine->nanoseconds / 100u) / machine->tick) * machine->tick;
}

static const AdvancedConfigurationAndPowerInterfaceHost_t timer_host = {
    .context = &timer_machine,
    .port_read = timer_port_read,
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
 * @brief Whether @p name is @p expected, terminating zero included.
 */
static bool timer_name_is(const char *name, const char *expected)
{
    return memcmp(name, expected, strlen(expected) + 1u) == 0;
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
    kernel_test_check(test, timer_machine.reads == 0u, "and no port was read");
}

/**
 * @brief A timer of 24 bits, and one of 32 bits crossing 2^24, that count 3.579545 MHz are usable,
 *        their rate a thousand thousandths of it within the 1 µs a read costs.
 */
KERNEL_TEST(a_timer_that_counts_3_579545_mhz_is_usable)
{
    uint8_t low_state;
    uint32_t low_rate;
    uint8_t wide_state;

    timer_open(0x00FFF000u);
    low_state = timer_find();
    low_rate = timer_found.rate_per_mille;
    timer_open(0x00FFF000u);
    timer_machine.bits = TIMER_ALL_BITS;
    wide_state = timer_find();

    kernel_test_check(test, low_state == TIMER_STATE(USABLE), "a 24-bit timer that wraps is usable");
    kernel_test_check(test, timer_found.port == TIMER_PORT, "its port is PM_TMR_BLK's");
    kernel_test_check(test, low_rate >= 999u && low_rate <= 1001u, "its rate is 3.579545 MHz within a read");
    kernel_test_check(test, wide_state == TIMER_STATE(USABLE), "a 32-bit timer that crosses 2^24 is usable");
    kernel_test_measure(test, "rate_per_mille", low_rate);
}

/**
 * @brief A timer that holds still is stopped after a bounded number of reads; one that goes back is
 *        inconsistent; one 6 % fast or slow has the wrong rate, and one 4 % fast or slow does not.
 */
KERNEL_TEST(a_timer_that_holds_still_goes_back_or_runs_off_rate_is_not_usable)
{
    uint8_t stopped;
    uint32_t stopped_reads;
    uint8_t backward;
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
    kernel_test_check(test, fast == TIMER_STATE(WRONG_RATE) && slow == TIMER_STATE(WRONG_RATE),
                      "6 % fast or slow is the wrong rate");
    kernel_test_check(test, near_fast == TIMER_STATE(USABLE) && near_slow == TIMER_STATE(USABLE),
                      "4 % fast or slow is usable");
    kernel_test_measure(test, "stopped_reads", stopped_reads);
}

/**
 * @brief A host whose clock stands still leaves the rate unchecked, once the timer has counted half a
 *        second waiting for it to move.
 */
KERNEL_TEST(a_clock_that_stands_still_leaves_the_rate_unchecked)
{
    uint8_t state;

    timer_open(0u);
    timer_machine.tick = 0u;
    timer_machine.access = TIMER_SLOW_ACCESS;
    state = timer_find();

    kernel_test_check(test, state == TIMER_STATE(NO_REFERENCE), "no step of the clock: no reference");
    kernel_test_check(test, timer_machine.nanoseconds >= 500000000ull && timer_machine.nanoseconds < 2000000000ull,
                      "after half a second or so of the timer's counting");
}

/**
 * @brief A stall waits the microseconds asked as the machine's time counts them, and says so: 1 µs,
 *        50 µs, and 100 µs across the wrap of a 24-bit timer and across 2^24 for a 32-bit one.
 */
KERNEL_TEST(a_stall_waits_its_microseconds_on_the_timer)
{
    uint32_t one_waited;
    uint32_t fifty_waited;
    uint32_t wrap_waited;
    uint32_t wide_waited;
    uint64_t one;
    uint64_t fifty;
    uint64_t wrap;
    uint64_t wide;

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

    kernel_test_check(test, one >= 1000u && one < 2000u, "1 µs lasts 1 µs to 2 µs");
    kernel_test_check(test, one_waited >= 1u && one_waited <= 2u, "and says so");
    kernel_test_check(test, fifty >= 50000u && fifty < 51000u, "50 µs lasts 50 µs to 51 µs");
    kernel_test_check(test, fifty_waited >= 50u && fifty_waited <= 51u, "and says so");
    kernel_test_check(test, wrap >= 100000u && wrap < 101000u, "100 µs across the wrap of 24 bits lasts 100 µs");
    kernel_test_check(test, wrap_waited >= 100u && wrap_waited <= 101u, "and says so");
    kernel_test_check(test, wide >= 100000u && wide < 101000u, "100 µs across 2^24 on 32 bits lasts 100 µs");
    kernel_test_check(test, wide_waited >= 100u && wide_waited <= 101u, "and says so");
    kernel_test_measure(test, "fifty_waited", fifty_waited);
}

/**
 * @brief A read caught in an increment does not end a stall early; a timer that stops during a stall
 *        ends it within a bounded number of reads, saying it waited less; a timer that is not usable
 *        is not waited on, and no port is read.
 */
KERNEL_TEST(a_stall_survives_a_bad_read_and_a_timer_that_stops)
{
    uint32_t glitch_waited;
    uint32_t stopped_waited;
    uint32_t unusable_waited;
    uint64_t glitch;
    uint32_t reads_before;
    uint32_t stopped_reads;

    timer_open(0u);
    timer_machine.access = TIMER_FAST_ACCESS;
    (void) timer_find();
    timer_machine.out_of_order = timer_machine.reads + 5u;
    glitch = timer_wait(50u, &glitch_waited);
    timer_machine.stops_after = timer_machine.reads + 10u;
    reads_before = timer_machine.reads;
    stopped_waited =
        advanced_configuration_and_power_interface_power_management_timer_stall(&timer_host, &timer_found, 200u);
    stopped_reads = timer_machine.reads - reads_before;
    timer_found.state = TIMER_STATE(STOPPED);
    reads_before = timer_machine.reads;
    unusable_waited =
        advanced_configuration_and_power_interface_power_management_timer_stall(&timer_host, &timer_found, 50u);

    kernel_test_check(test, glitch >= 50000u && glitch_waited >= 50u,
                      "a read caught in an increment ends nothing early");
    kernel_test_check(test, stopped_waited < 200u, "a timer that stops ends the stall, saying it waited less");
    kernel_test_check(test,
                      stopped_reads <=
                          3u * (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_READ_CHECKS + 20u),
                      "within a bounded number of reads");
    kernel_test_check(test, unusable_waited == 0u && timer_machine.reads == reads_before,
                      "a timer that is not usable is not waited on");
}

/**
 * @brief Each state has the name the boot's telemetry prints, and a value past them is unknown.
 */
KERNEL_TEST(each_timer_state_has_its_name)
{
    const char *const names[] = {"absent",       "unreachable", "stopped", "inconsistent",
                                 "no-reference", "wrong-rate",  "usable"};
    bool every_name_held = true;

    for (uint8_t state = 0u; state < sizeof(names) / sizeof(names[0]); ++state)
        every_name_held =
            every_name_held &&
            timer_name_is(advanced_configuration_and_power_interface_power_management_timer_state_name(state),
                          names[state]);

    kernel_test_check(test, every_name_held,
                      "absent, unreachable, stopped, inconsistent, no-reference, wrong-rate, usable");
    kernel_test_check(
        test,
        timer_name_is(advanced_configuration_and_power_interface_power_management_timer_state_name(7u), "unknown"),
        "and 7 is unknown");
}
