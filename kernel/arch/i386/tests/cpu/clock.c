#include <kernel/cpu/clock.h>
#include <kernel/drivers/rtc.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(clock, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief The periodic tick has a rate and advances while the processor spins.
 *
 * @details Bounded: a tick that never comes fails the test instead of hanging the boot.
 */
KERNEL_TEST(periodic_tick_advances)
{
    const uint32_t ticks_before = clock_get_tick_count();
    bool advanced = false;

    for (uint32_t spin = 0u; spin < 1000000u && !advanced; ++spin)
        advanced = (clock_get_tick_count() != ticks_before);

    kernel_test_check(test, clock_get_tick_hz() != 0u, "the tick has a rate");
    kernel_test_check(test, advanced, "the tick advances while the processor spins");
    kernel_test_measure(test, "tick_hz", clock_get_tick_hz());
    kernel_test_measure(test, "spurious_irq7", clock_get_spurious_irq7_count());
    kernel_test_measure(test, "spurious_irq15", clock_get_spurious_irq15_count());
    kernel_test_measure(test, "rtc_periodic", clock_is_rtc_periodic_enabled());
}

/**
 * @brief The wall clock reads a date and a time whose every field is in range.
 */
KERNEL_TEST(wall_clock_reads_a_valid_date)
{
    RealtimeClockTime_t now;

    clock_read_walltime(&now);

    kernel_test_check(test, now.second < 60u && now.minute < 60u && now.hour < 24u, "the time of day is in range");
    kernel_test_check(test, now.day >= 1u && now.day <= 31u && now.month >= 1u && now.month <= 12u,
                      "the day and the month are in range");
    kernel_test_check(test, now.year >= 2000u, "the year is read in full");
    kernel_test_measure(test, "year", now.year);
}
