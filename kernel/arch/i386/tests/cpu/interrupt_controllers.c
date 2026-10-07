#include <kernel/cpu/acpi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(interrupt_controllers, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief The firmware describes the local interrupt controllers, and the kernel maps them.
 */
KERNEL_TEST(local_controller_is_ready)
{
    kernel_test_check(test, advanced_configuration_and_power_interface_madt_is_available(),
                      "the firmware has an interrupt controller table");
    kernel_test_check(test, advanced_configuration_and_power_interface_madt_get_local_apic_count() > 0u,
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
 * @brief When the local controller's timer owns the tick in periodic mode, eight ticks arrive.
 */
KERNEL_TEST(local_timer_ticks_periodically)
{
    if (!interrupt_request_is_timer_owner_apic() || !advanced_pic_timer_backend_is_periodic_mode_enabled())
    {
        kernel_test_skip(test, "the local timer does not own the tick in periodic mode");
        return;
    }

    const uint32_t start = clock_get_tick_count();

    for (uint32_t spin = 0u; spin < 50000000u && clock_get_tick_count() - start < 8u; ++spin)
        asmutils_no_operation();

    kernel_test_check(test, clock_get_tick_count() - start >= 8u, "eight ticks arrive within a bounded spin");
}
