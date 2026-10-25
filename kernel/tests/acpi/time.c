#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_time, KERNEL_TEST_STAGE_INITIALIZATION);

#define TIME_TABLE_CAPACITY 512u   /**< Bytes of the buffer a made-up DSDT is written in. */
#define TIME_MILLISECOND    10000u /**< A millisecond on the virtual clock, in units of 100 ns. */
#define TIME_MICROSECOND    10u    /**< A microsecond on the virtual clock. */
#define TIME_STATUS(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name

static uint8_t time_table[TIME_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t time_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t time_load;

/**
 * @brief Methods that sleep 60 s per turn of an endless loop (S60K), 3 s (S3K_), 3 s Arg0 times (SLPN), 70 ms until
 * refused (S70_), and stall Arg0 µs (STAL); TICK reads Timer twice, TSLP around a Sleep (5), POLL loops until Timer
 * passes 10 ms ahead, TIMR reads it once, NONE sleeps and stalls 0. TURN counts the turns of the loops.
 */
static const uint8_t time_aml[] = {
    0x08u, 'T',   'U',   'R',   'N',   0x0Au, 0x00u, 0x14u, 0x0Eu, 'S',   '6',   '0',   'K',   0x00u, 0xA2u, 0x07u,
    0x01u, 0x5Bu, 0x22u, 0x0Bu, 0x60u, 0xEAu, 0x14u, 0x0Bu, 'S',   '3',   'K',   '_',   0x00u, 0x5Bu, 0x22u, 0x0Bu,
    0xB8u, 0x0Bu, 0x14u, 0x1Fu, 'S',   'L',   'P',   'N',   0x01u, 0x70u, 0x0Au, 0x00u, 'T',   'U',   'R',   'N',
    0xA2u, 0x11u, 0x95u, 'T',   'U',   'R',   'N',   0x68u, 0x5Bu, 0x22u, 0x0Bu, 0xB8u, 0x0Bu, 0x75u, 'T',   'U',
    'R',   'N',   0x14u, 0x19u, 'S',   '7',   '0',   '_',   0x00u, 0x70u, 0x0Au, 0x00u, 'T',   'U',   'R',   'N',
    0xA2u, 0x0Bu, 0x01u, 0x5Bu, 0x22u, 0x0Au, 0x46u, 0x75u, 'T',   'U',   'R',   'N',   0x14u, 0x09u, 'S',   'T',
    'A',   'L',   0x01u, 0x5Bu, 0x21u, 0x68u, 0x14u, 0x13u, 'T',   'I',   'C',   'K',   0x00u, 0x70u, 0x5Bu, 0x33u,
    0x60u, 0x70u, 0x5Bu, 0x33u, 0x61u, 0xA4u, 0x74u, 0x61u, 0x60u, 0x00u, 0x14u, 0x14u, 'T',   'S',   'L',   'P',
    0x00u, 0x70u, 0x5Bu, 0x33u, 0x60u, 0x5Bu, 0x22u, 0x0Au, 0x05u, 0xA4u, 0x74u, 0x5Bu, 0x33u, 0x60u, 0x00u, 0x14u,
    0x1Eu, 'P',   'O',   'L',   'L',   0x00u, 0x70u, 0x0Au, 0x00u, 0x61u, 0x72u, 0x5Bu, 0x33u, 0x0Cu, 0xA0u, 0x86u,
    0x01u, 0x00u, 0x60u, 0xA2u, 0x08u, 0x92u, 0x94u, 0x5Bu, 0x33u, 0x60u, 0x75u, 0x61u, 0xA4u, 0x61u, 0x14u, 0x09u,
    'T',   'I',   'M',   'R',   0x00u, 0xA4u, 0x5Bu, 0x33u, 0x14u, 0x0Eu, 'N',   'O',   'N',   'E',   0x00u, 0x5Bu,
    0x22u, 0x0Au, 0x00u, 0x5Bu, 0x21u, 0x0Au, 0x00u,
};

/**
 * @brief Loads the table as a DSDT of @p revision, on the simulated host as it stands.
 */
static bool time_load_on_host(KernelTest_t *test, uint8_t revision)
{
    return made_up_load_on(test, &time_load, &time_host.host, time_table, revision, time_aml, sizeof(time_aml));
}

/**
 * @brief Opens a fresh simulated host whose tick lasts @p tick_milliseconds, 0 for none, and loads the
 *        table as a DSDT of revision 2.
 */
static bool time_run(KernelTest_t *test, uint32_t tick_milliseconds)
{
    advanced_configuration_and_power_interface_simulated_host_open(&time_host);
    time_host.tick_milliseconds = tick_milliseconds;
    return time_load_on_host(test, 2u);
}

/**
 * @brief Evaluates the method at @p path with @p argument_count Integer arguments, the first
 *        @p argument, and gives its Integer, or 0xDEAD; its status goes to @p out_status.
 */
static uint64_t time_evaluate(const char *path, uint32_t argument_count, uint64_t argument,
                              AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t *out_status)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t arguments[1] = {
        {.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER, .payload = argument},
    };
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    uint64_t integer = 0xDEADu;

    *out_status = advanced_configuration_and_power_interface_machine_language_evaluate(&time_load, path, arguments,
                                                                                       argument_count, &result);
    if (*out_status == TIME_STATUS(OK) &&
        result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        integer = result.payload;
    advanced_configuration_and_power_interface_machine_language_value_release(&time_load.heap, &time_load.space,
                                                                              &result);
    return integer;
}

/**
 * @brief The integer TURN holds, or 0xDEAD.
 */
static uint64_t time_turns(void)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&time_load, "TURN");

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Closes the load and gives the simulated memory back.
 */
static void time_close(void)
{
    advanced_configuration_and_power_interface_machine_language_close(&time_load);
    advanced_configuration_and_power_interface_simulated_host_close(&time_host);
}

/**
 * @brief An endless loop that sleeps 60 s per turn asks for more than the 30 s an evaluation may
 *        request on its first turn: it fails with limit_time_requested before the host waits at all
 *        (§5.5, §6.1, §8.2).
 */
KERNEL_TEST(a_sleep_of_60_s_is_refused_before_it_sleeps)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);

    if (!time_run(test, 0u))
        return;
    (void) time_evaluate("\\S60K", 0u, 0u, &status);
    kernel_test_check(test, status == TIME_STATUS(LIMIT_TIME_REQUESTED), "S60K fails with limit_time_requested");
    kernel_test_check(test,
                      time_host.clock_100ns == 0u && time_load.services.sleeps == 0u &&
                          time_load.services.waited_milliseconds == 0u,
                      "the host never slept");
    time_close();
}

/**
 * @brief A Sleep (3000) waits its 3 s: no 2 s cap, as ACPICA and uACPI have (§5.5).
 */
KERNEL_TEST(a_sleep_of_3_s_is_honoured_without_a_2_s_cap)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);

    if (!time_run(test, 0u))
        return;
    (void) time_evaluate("\\S3K_", 0u, 0u, &status);
    kernel_test_check(test, status == TIME_STATUS(OK), "S3K_ runs");
    kernel_test_check(test,
                      time_host.clock_100ns == 3000u * TIME_MILLISECOND &&
                          time_load.services.sleep_milliseconds == 3000u &&
                          time_load.services.waited_milliseconds == 3000u && time_load.services.sleeps == 1u,
                      "the host slept 3,000 ms, as requested and as counted");
    time_close();
}

/**
 * @brief Ten sleeps of 3 s fill an evaluation's 30 s; an eleventh is refused, after the ten have
 *        slept; the next evaluation has 30 s of its own.
 */
KERNEL_TEST(an_evaluation_may_request_30_s_and_no_more)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);

    if (!time_run(test, 0u))
        return;
    (void) time_evaluate("\\SLPN", 1u, 10u, &status);
    kernel_test_check(test, status == TIME_STATUS(OK) && time_turns() == 10u, "ten sleeps of 3 s run");
    (void) time_evaluate("\\SLPN", 1u, 11u, &status);
    kernel_test_check(test, status == TIME_STATUS(LIMIT_TIME_REQUESTED) && time_turns() == 10u,
                      "in the next evaluation, ten run again and the eleventh is refused");
    kernel_test_check(
        test, time_host.clock_100ns == 60000u * TIME_MILLISECOND && time_load.services.time_requested_peak == 30000000u,
        "the host slept 60 s in all, and no evaluation requested more than 30 s");
    time_close();
}

/**
 * @brief The budget counts the milliseconds AML requests, never those the host waited: on a host
 *        without a tick, one of 1 ms and one of 10 ms, a loop of Sleep (70) is refused on the same
 *        turn, 429, while the time waited differs with the tick.
 */
KERNEL_TEST(the_budget_counts_what_is_requested_never_what_is_waited)
{
    static const struct {
        uint32_t tick_milliseconds;
        uint32_t waited_milliseconds;
    } hosts[] = {
        {0u,  428u * 70u},
        {1u,  428u * 71u},
        {10u, 428u * 80u},
    };
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);
    bool same_turns = true;
    bool waits_follow_the_tick = true;

    for (uint32_t index = 0u; index < sizeof(hosts) / sizeof(hosts[0]); ++index)
    {
        if (!time_run(test, hosts[index].tick_milliseconds))
            return;
        (void) time_evaluate("\\S70_", 0u, 0u, &status);
        same_turns = same_turns && status == TIME_STATUS(LIMIT_TIME_REQUESTED) && time_turns() == 428u;
        waits_follow_the_tick = waits_follow_the_tick &&
                                time_load.services.waited_milliseconds == hosts[index].waited_milliseconds &&
                                time_host.clock_100ns == (uint64_t) hosts[index].waited_milliseconds * TIME_MILLISECOND;
        kernel_test_measure(test, "turns", (uint32_t) time_turns());
        time_close();
    }
    kernel_test_check(test, same_turns, "each host runs 428 sleeps of 70 ms, and refuses the 429th");
    kernel_test_check(test, waits_follow_the_tick, "each host waited what its tick rounds the sleeps up to");
}

/**
 * @brief A Stall of up to 255 µs busy-waits that long; beyond, it sleeps the milliseconds it rounds
 *        up to; both are counted beyond 100 µs (§5.5).
 */
KERNEL_TEST(a_stall_busy_waits_up_to_255_us_and_sleeps_beyond)
{
    static const uint32_t stalls[] = {50u, 100u, 101u, 255u, 256u, 1001u};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);
    bool ran = true;

    if (!time_run(test, 0u))
        return;
    for (uint32_t index = 0u; index < sizeof(stalls) / sizeof(stalls[0]); ++index)
    {
        (void) time_evaluate("\\STAL", 1u, stalls[index], &status);
        ran = ran && status == TIME_STATUS(OK);
    }
    kernel_test_check(test, ran, "every stall runs");
    kernel_test_check(test,
                      time_host.clock_100ns == (50u + 100u + 101u + 255u) * TIME_MICROSECOND + 3u * TIME_MILLISECOND,
                      "the host busy-waited 506 µs, then slept 1 ms for 256 µs and 2 ms for 1,001 µs");
    kernel_test_check(test,
                      time_load.services.stalls == 6u && time_load.services.long_stalls == 4u &&
                          time_load.services.stalls_slept == 2u && time_load.services.stall_microseconds == 1763u &&
                          time_load.services.waited_milliseconds == 3u,
                      "six stalls, four beyond 100 µs, two of them slept, 1,763 µs requested");
    time_close();
}

/**
 * @brief A Stall is charged to the same budget: 30,000,000 µs are allowed, one more is refused before
 *        any wait.
 */
KERNEL_TEST(a_stall_past_the_budget_is_refused)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);

    if (!time_run(test, 0u))
        return;
    (void) time_evaluate("\\STAL", 1u, 30000001u, &status);
    kernel_test_check(test, status == TIME_STATUS(LIMIT_TIME_REQUESTED) && time_host.clock_100ns == 0u,
                      "30,000,001 µs are refused, and the host never waited");
    (void) time_evaluate("\\STAL", 1u, 30000000u, &status);
    kernel_test_check(test, status == TIME_STATUS(OK) && time_host.clock_100ns == 30000u * TIME_MILLISECOND,
                      "30,000,000 µs are slept as 30,000 ms");
    time_close();
}

/**
 * @brief Timer counts in units of 100 ns: two reads in a row differ by the virtual clock's quantum,
 *        and two reads around a Sleep (5) by 5 ms more.
 */
KERNEL_TEST(timer_counts_100_ns_and_advances_on_each_read)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);

    if (!time_run(test, 0u))
        return;
    kernel_test_check(test,
                      time_evaluate("\\TICK", 0u, 0u, &status) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_TIMER_QUANTUM,
                      "two reads differ by the quantum, 100 µs");
    kernel_test_check(test,
                      time_evaluate("\\TSLP", 0u, 0u, &status) ==
                          5u * TIME_MILLISECOND +
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_TIMER_QUANTUM,
                      "two reads around Sleep (5) differ by 5 ms and the quantum");
    kernel_test_check(test, time_load.services.timer_reads == 4u, "four reads counted");
    time_close();
}

/**
 * @brief A loop that polls Timer until it passes a deadline 10 ms ahead ends after the same number of
 *        turns on every target, 100, since each read advances the virtual clock by 100 µs (§2).
 */
KERNEL_TEST(a_timer_poll_loop_ends_the_same_way_everywhere)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);
    uint64_t turns = 0u;

    if (!time_run(test, 0u))
        return;
    turns = time_evaluate("\\POLL", 0u, 0u, &status);
    kernel_test_check(test, status == TIME_STATUS(OK) && turns == 100u, "the loop takes 100 turns");
    kernel_test_measure(test, "turns", (uint32_t) turns);
    time_close();
}

/**
 * @brief Under a revision-1 DSDT, Timer is masked to 32 bits like every other integer.
 */
KERNEL_TEST(timer_is_masked_to_32_bits_under_revision_1)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);

    advanced_configuration_and_power_interface_simulated_host_open(&time_host);
    time_host.clock_100ns = 0x100000007ull;
    if (!time_load_on_host(test, 1u))
        return;
    kernel_test_check(test, time_evaluate("\\TIMR", 0u, 0u, &status) == 7u, "a clock of 2^32 + 7 reads 7");
    time_close();
    advanced_configuration_and_power_interface_simulated_host_open(&time_host);
    time_host.clock_100ns = 0x100000007ull;
    if (!time_load_on_host(test, 2u))
        return;
    kernel_test_check(test, time_evaluate("\\TIMR", 0u, 0u, &status) == 0x100000007ull,
                      "under revision 2 it reads whole");
    time_close();
}

/**
 * @brief Sleep (0) and Stall (0) wait nothing, and are counted.
 */
KERNEL_TEST(a_sleep_and_a_stall_of_0_wait_nothing)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = TIME_STATUS(OK);

    if (!time_run(test, 10u))
        return;
    (void) time_evaluate("\\NONE", 0u, 0u, &status);
    kernel_test_check(test,
                      status == TIME_STATUS(OK) && time_host.clock_100ns == 0u && time_load.services.sleeps == 1u &&
                          time_load.services.stalls == 1u,
                      "neither waits, even on a host whose tick would round them up");
    time_close();
}
