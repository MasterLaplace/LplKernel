#include <kernel/acpi/tables.h>
#include <kernel/boot/boot_module.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

#define INTERRUPT_CONTROLLERS_TEST_TICKS            8u  /**< Ticks the contract waits for. */
#define INTERRUPT_CONTROLLERS_TEST_DEADLINE_PERIODS 12u /**< Tick periods the wait may last, half again as many. */

KERNEL_TEST_SUITE(interrupt_controllers, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief The tick's contract, whichever timer keeps it: the profile's frequency, and ticks that arrive.
 *
 * @details The wait ends on a deadline rather than on a count of loop iterations, which ran out before
 *          eight ticks under KVM. A stopped tick ends it too, after the deadline's stalled polls.
 */
static void interrupt_controllers_check_the_tick_contract(KernelTest_t *test)
{
    const uint32_t tick_hz = clock_get_tick_hz();
    const uint32_t start = clock_get_tick_count();
    InterruptRequestDeadline_t deadline;

    kernel_test_check(test, tick_hz != 0u && tick_hz == clock_get_profile_tick_hz(),
                      "the tick keeps the profile's frequency");
    if (tick_hz == 0u)
        return;

    interrupt_request_deadline_start(&deadline, INTERRUPT_CONTROLLERS_TEST_DEADLINE_PERIODS * (1000000u / tick_hz));
    while (clock_get_tick_count() - start < INTERRUPT_CONTROLLERS_TEST_TICKS &&
           !interrupt_request_deadline_passed(&deadline))
        asmutils_pause();

    kernel_test_check(test, clock_get_tick_count() - start >= INTERRUPT_CONTROLLERS_TEST_TICKS,
                      "eight ticks arrive before the deadline");
}

/**
 * @brief The firmware describes the local interrupt controllers, and the kernel maps them.
 */
KERNEL_TEST(local_controller_is_ready)
{
    kernel_test_check(test, advanced_configuration_and_power_interface_madt_is_available(),
                      "the firmware has an interrupt controller table");
    kernel_test_check(test, advanced_configuration_and_power_interface_madt()->processor_count > 0u,
                      "it lists at least one local controller");
    kernel_test_check(test, advanced_configuration_and_power_interface_madt_get_local_apic_physical_base() != 0u,
                      "it gives the local controllers' address");
    kernel_test_check(test, advanced_pic_timer_backend_is_local_apic_mmio_mapped(),
                      "the kernel mapped the local controller's registers");
}

/**
 * @brief The firmware describes an input/output interrupt controller, and the kernel routes lines
 *        through it.
 */
KERNEL_TEST(input_output_controller_is_ready)
{
    kernel_test_check(test, advanced_configuration_and_power_interface_madt_get_io_apic_count() > 0u,
                      "the firmware lists an input/output controller");
    kernel_test_check(test, input_output_advanced_programmable_interrupt_controller_get_mapped_count() > 0u,
                      "the kernel mapped one");
    kernel_test_check(test, input_output_advanced_programmable_interrupt_controller_get_programmed_route_count() > 0u,
                      "and programmed routes through it");
}

/**
 * @brief In every profile the local controller's timer owns the tick, in periodic mode, one tick per
 *        period.
 */
KERNEL_TEST(local_timer_owns_the_tick)
{
    if (boot_command_line_holds(CLOCK_KEEP_THE_PIT_OPTION))
    {
        kernel_test_skip(test, "the boot line keeps the tick on the PIT");
        return;
    }

    const uint32_t local_timer_hz = advanced_pic_timer_backend_get_calibrated_timer_frequency_hz();
    const uint32_t tick_hz = clock_get_tick_hz();

    kernel_test_check(test, local_timer_hz != 0u, "the local timer was calibrated against the PIT");
    kernel_test_check(test, interrupt_request_is_timer_owner_apic(), "it owns the tick");
    kernel_test_check(test, advanced_pic_timer_backend_is_periodic_mode_enabled(), "in periodic mode");
    kernel_test_check(test, tick_hz != 0u && apic_read(LAPIC_REG_TIMER_INIT) == local_timer_hz / tick_hz,
                      "and its period is one tick");
    kernel_test_measure(test, "local_timer_hz", local_timer_hz);
    interrupt_controllers_check_the_tick_contract(test);
}

/**
 * @brief When the boot line declines the local timer, the PIT keeps the tick on the same contract.
 *
 * @details The option skips the calibration, so the kernel takes the path a failed calibration takes.
 */
KERNEL_TEST(the_pit_keeps_the_tick_when_the_local_timer_is_declined)
{
    if (!boot_command_line_holds(CLOCK_KEEP_THE_PIT_OPTION))
    {
        kernel_test_skip(test, "the boot line does not decline the local timer");
        return;
    }

    kernel_test_check(test, advanced_pic_timer_backend_get_calibrated_timer_frequency_hz() == 0u,
                      "the local timer was not calibrated");
    kernel_test_check(test, !interrupt_request_is_timer_owner_apic(), "the PIT owns the tick");
    kernel_test_check(test, !advanced_pic_timer_backend_is_periodic_mode_enabled(), "the local timer does not tick");
    interrupt_controllers_check_the_tick_contract(test);
}
