#include <kernel/acpi/tables.h>
#include <kernel/boot/boot_module.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

#include <string.h>

#define INTERRUPT_CONTROLLERS_TEST_TICKS 8u /**< Ticks the contract waits for. */
#define INTERRUPT_CONTROLLERS_TEST_DEADLINE_PERIODS                                                                    \
    12u /**< The deadline's length in tick periods: more than eight, since it counts the same tick. */
#define INTERRUPT_CONTROLLERS_TEST_HIGHEST_NAMEABLE_PROCESSOR                                                          \
    0xFEu /**< The highest APIC ID a physical destination names: 0xFF broadcasts (SDM 325384-093US §13.6.2.1). */
#define INTERRUPT_CONTROLLERS_TEST_BROADCAST             0xFFu  /**< A physical destination's broadcast. */
#define INTERRUPT_CONTROLLERS_TEST_PAST_EIGHT_BITS       0x100u /**< The first APIC ID past a destination's 8 bits. */
#define INTERRUPT_CONTROLLERS_TEST_ANOTHER_PROCESSOR     0x2Au /**< An identifier QEMU never gives its first processor. */
#define INTERRUPT_CONTROLLERS_TEST_YET_ANOTHER_PROCESSOR 0x15u /**< The one taken when a route already names 0x2A. */
#define INTERRUPT_CONTROLLERS_TEST_UNNAMEABLE_STATE                                                                    \
    "ioapic-init-bootstrap-processor-unnameable" /**< The state when no entry can name the bootstrap processor. */

KERNEL_TEST_SUITE(interrupt_controllers, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief What the routing scaffold programmed for one identifier of the bootstrap processor.
 */
typedef struct {
    uint8_t routes;        /**< Routes it programmed. */
    uint8_t routes_naming; /**< Of those, routes whose redirection entry names the identifier. */
    uint32_t refused;      /**< Destinations it refused. */
    const char *state;     /**< The state it ended in. */
} InterruptControllersRouting_t;

static bool interrupt_controllers_a_route_is_live(void)
{
    const uint8_t route_count = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();

    for (uint8_t route_index = 0u; route_index < route_count; ++route_index)
    {
        if (!input_output_advanced_programmable_interrupt_controller_get_programmed_route_is_masked(route_index))
            return true;
    }
    return false;
}

static uint8_t interrupt_controllers_routes_naming(uint32_t apic_id)
{
    const uint8_t route_count = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();
    uint8_t routes_naming = 0u;

    for (uint8_t route_index = 0u; route_index < route_count; ++route_index)
    {
        if (input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(
                route_index) == apic_id)
            ++routes_naming;
    }
    return routes_naming;
}

static bool interrupt_controllers_state_is(const char *state, const char *name)
{
    return state != NULL && strlen(state) == strlen(name) && memcmp(state, name, strlen(name)) == 0;
}

/**
 * @brief Why the lines cannot be routed again on this machine, or NULL when they can.
 */
static const char *interrupt_controllers_why_the_lines_cannot_be_routed_again(void)
{
    if (input_output_advanced_programmable_interrupt_controller_get_programmed_route_count() == 0u)
        return "the boot routed no line through an input/output controller";
    if (interrupt_controllers_a_route_is_live())
        return "a route is live, and routing it again would mask it and drop its interrupts";
    return NULL;
}

/**
 * @brief Routes the lines again as if the bootstrap processor had @p apic_id, then, the topology put
 *        back, routes them again to the bootstrap processor.
 *
 * @details The boot's move of IRQ1 is not replayed: it names the processor the scaffold names.
 */
static InterruptControllersRouting_t interrupt_controllers_route_as_if_the_bootstrap_processor_were(uint32_t apic_id)
{
    CpuTopologySnapshot_t found;
    InterruptControllersRouting_t routing;

    cpu_topology_debug_save(&found);
    cpu_topology_set_runtime_local_apic_id(apic_id);
    input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold();
    routing.routes = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();
    routing.routes_naming = interrupt_controllers_routes_naming(apic_id);
    routing.refused = input_output_advanced_programmable_interrupt_controller_get_destinations_refused();
    routing.state = input_output_advanced_programmable_interrupt_controller_get_state_name();
    cpu_topology_debug_restore(&found);
    input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold();
    return routing;
}

static bool interrupt_controllers_routes_are_put_back(uint8_t routes)
{
    return input_output_advanced_programmable_interrupt_controller_get_programmed_route_count() == routes &&
           interrupt_controllers_routes_naming(cpu_topology_get_local_apic_id()) == routes;
}

static int32_t interrupt_controllers_find_a_masked_route(void)
{
    const uint8_t route_count = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();

    for (uint8_t route_index = 0u; route_index < route_count; ++route_index)
    {
        if (input_output_advanced_programmable_interrupt_controller_get_programmed_route_is_masked(route_index))
            return (int32_t) route_index;
    }
    return -1;
}

static uint8_t interrupt_controllers_move_route(uint8_t isa_irq, uint32_t apic_id)
{
    return input_output_advanced_programmable_interrupt_controller_set_isa_route_destination(isa_irq, apic_id);
}

static bool interrupt_controllers_a_processor_has(uint32_t apic_id)
{
    const AdvancedConfigurationAndPowerInterfaceMadtState_t *madt = advanced_configuration_and_power_interface_madt();

    for (uint32_t slot = 0u; slot < madt->processor_count; ++slot)
    {
        if (madt->processors[slot].apic_id == apic_id)
            return true;
    }
    return false;
}

/**
 * @brief The lowest APIC ID a physical destination names that no processor of the MADT has, or
 *        CPU_TOPOLOGY_NO_PROCESSOR when every one of them has a processor.
 */
static uint32_t interrupt_controllers_find_an_absent_processor(void)
{
    for (uint32_t apic_id = 1u; apic_id <= INTERRUPT_CONTROLLERS_TEST_HIGHEST_NAMEABLE_PROCESSOR; ++apic_id)
    {
        if (!interrupt_controllers_a_processor_has(apic_id) && apic_id != cpu_topology_get_local_apic_id())
            return apic_id;
    }
    return CPU_TOPOLOGY_NO_PROCESSOR;
}

static uint8_t interrupt_controllers_lines_left_unmasked(const uint8_t *lines, uint8_t line_count)
{
    uint8_t unmasked = 0u;

    for (uint8_t index = 0u; index < line_count; ++index)
    {
        uint8_t masked = 1u;

        if (input_output_advanced_programmable_interrupt_controller_read_line_mask(lines[index], &masked) && !masked)
            ++unmasked;
    }
    return unmasked;
}

/**
 * @brief The tick's contract, whichever timer keeps it: the profile's frequency, and ticks that arrive.
 *
 * @details The wait ends after eight ticks, or, should the tick stop, once the deadline's limit of
 *          polls without a tick runs out. A count of loop iterations ran out before eight ticks under
 *          KVM. The deadline counts the same tick, so the pace of the ticks is not checked here.
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
                      "eight ticks arrive before the tick stalls");
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
 * @brief Every route the boot programmed names the bootstrap processor, read back from its
 *        redirection entry.
 *
 * @details The routes named APIC ID 0, which is the bootstrap processor only when its identifier is 0,
 *          as it always is under QEMU: the next tests give the topology other identifiers to tell the
 *          two apart.
 */
KERNEL_TEST(every_route_names_the_bootstrap_processor)
{
    const uint32_t bootstrap = cpu_topology_get_local_apic_id();
    const uint8_t routes = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();

    if (input_output_advanced_programmable_interrupt_controller_get_mapped_count() == 0u)
    {
        kernel_test_skip(test, "the kernel mapped no input/output controller");
        return;
    }
    kernel_test_measure(test, "bootstrap_apic_id", bootstrap);
    if (bootstrap > INTERRUPT_CONTROLLERS_TEST_HIGHEST_NAMEABLE_PROCESSOR)
    {
        kernel_test_check(test, routes == 0u, "no line is routed to a processor no redirection entry can name");
        kernel_test_check(test, input_output_advanced_programmable_interrupt_controller_get_destinations_refused() > 0u,
                          "and each line refused is counted");
        return;
    }
    kernel_test_check(test, routes > 0u, "lines are routed");
    kernel_test_check(test, interrupt_controllers_routes_naming(bootstrap) == routes,
                      "every route names the bootstrap processor");
}

/**
 * @brief The routes follow the bootstrap processor's identifier, up to 0xFE, the highest a physical
 *        destination names.
 *
 * @details The topology is given another identifier and the lines are routed again, then routed back
 *          once it is restored. Every route named APIC ID 0, whatever the identifier.
 */
KERNEL_TEST(routes_follow_the_bootstrap_processor_identifier)
{
    const char *why_not = interrupt_controllers_why_the_lines_cannot_be_routed_again();

    if (why_not != NULL)
    {
        kernel_test_skip(test, why_not);
        return;
    }

    const uint8_t routes = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();
    const InterruptControllersRouting_t another =
        interrupt_controllers_route_as_if_the_bootstrap_processor_were(INTERRUPT_CONTROLLERS_TEST_ANOTHER_PROCESSOR);
    const InterruptControllersRouting_t highest = interrupt_controllers_route_as_if_the_bootstrap_processor_were(
        INTERRUPT_CONTROLLERS_TEST_HIGHEST_NAMEABLE_PROCESSOR);

    kernel_test_check(test, another.routes == routes && highest.routes == routes,
                      "the same lines are routed whichever identifier the bootstrap processor has");
    kernel_test_check(test, another.routes_naming == routes, "every route names 0x2A when the bootstrap processor is");
    kernel_test_check(test, highest.routes_naming == routes,
                      "and 0xFE, the highest identifier a physical destination names");
    kernel_test_check(test, another.refused == 0u && highest.refused == 0u, "neither is refused");
    kernel_test_check(test, interrupt_controllers_routes_are_put_back(routes),
                      "the routes are put back as the boot programmed them");
    kernel_test_measure(test, "routes", routes);
}

/**
 * @brief A bootstrap processor no redirection entry can name gets no route, and each line refused is
 *        counted: 0xFF, which a physical destination takes for a broadcast, and 0x100, past its 8 bits.
 *
 * @details Both were routed to APIC ID 0.
 */
KERNEL_TEST(a_bootstrap_processor_no_entry_can_name_gets_no_route)
{
    const char *why_not = interrupt_controllers_why_the_lines_cannot_be_routed_again();

    if (why_not != NULL)
    {
        kernel_test_skip(test, why_not);
        return;
    }

    const uint8_t routes = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();
    const InterruptControllersRouting_t broadcast =
        interrupt_controllers_route_as_if_the_bootstrap_processor_were(INTERRUPT_CONTROLLERS_TEST_BROADCAST);
    const InterruptControllersRouting_t past_eight_bits =
        interrupt_controllers_route_as_if_the_bootstrap_processor_were(INTERRUPT_CONTROLLERS_TEST_PAST_EIGHT_BITS);

    kernel_test_check(test, broadcast.routes == 0u, "0xFF, a broadcast to a physical destination, gets no route");
    kernel_test_check(test, past_eight_bits.routes == 0u, "nor does 0x100, past the destination's 8 bits");
    kernel_test_check(test, broadcast.refused == routes && past_eight_bits.refused == routes,
                      "each line refused is counted");
    kernel_test_check(
        test,
        interrupt_controllers_state_is(broadcast.state, INTERRUPT_CONTROLLERS_TEST_UNNAMEABLE_STATE) &&
            interrupt_controllers_state_is(past_eight_bits.state, INTERRUPT_CONTROLLERS_TEST_UNNAMEABLE_STATE),
        "the controller's state says why, apart from a line that does not resolve");
    kernel_test_check(test, interrupt_controllers_routes_are_put_back(routes),
                      "the routes are put back as the boot programmed them");
}

/**
 * @brief A line refused for its destination is left masked, as the scaffold leaves every line it
 *        programs, whatever the entry held before.
 *
 * @details The routed lines are first unmasked, aimed at a processor the MADT does not list so that
 *          nothing receives them, then routed again for 0x100. A refusal returned without writing the
 *          entry, and left it as it was.
 */
KERNEL_TEST(a_refused_line_is_left_masked)
{
    const char *why_not = interrupt_controllers_why_the_lines_cannot_be_routed_again();

    if (why_not != NULL)
    {
        kernel_test_skip(test, why_not);
        return;
    }

    const uint32_t absent = interrupt_controllers_find_an_absent_processor();

    if (absent == CPU_TOPOLOGY_NO_PROCESSOR)
    {
        kernel_test_skip(test, "every identifier a physical destination names has a processor");
        return;
    }

    const uint8_t routes = input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();
    uint8_t lines[INPUT_OUTPUT_APIC_MAX_ROUTES];
    CpuTopologySnapshot_t found;

    for (uint8_t route_index = 0u; route_index < routes; ++route_index)
    {
        lines[route_index] =
            input_output_advanced_programmable_interrupt_controller_get_programmed_route_irq(route_index);
        (void) interrupt_controllers_move_route(lines[route_index], absent);
        (void) input_output_advanced_programmable_interrupt_controller_enable_isa_route(lines[route_index]);
    }
    const uint8_t unmasked_before = interrupt_controllers_lines_left_unmasked(lines, routes);

    cpu_topology_debug_save(&found);
    cpu_topology_set_runtime_local_apic_id(INTERRUPT_CONTROLLERS_TEST_PAST_EIGHT_BITS);
    input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold();
    const uint8_t unmasked_after = interrupt_controllers_lines_left_unmasked(lines, routes);
    cpu_topology_debug_restore(&found);
    input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold();

    kernel_test_check(test, unmasked_before == routes,
                      "the routed lines are unmasked first, aimed at a processor that does not exist");
    kernel_test_check(test, unmasked_after == 0u, "a line refused for its destination is left masked");
    kernel_test_check(test,
                      interrupt_controllers_routes_are_put_back(routes) && !interrupt_controllers_a_route_is_live() &&
                          interrupt_controllers_lines_left_unmasked(lines, routes) == 0u,
                      "the routes are put back masked, as the boot programmed them");
    kernel_test_measure(test, "absent_apic_id", absent);
}

/**
 * @brief Moving a route refuses an identifier its redirection entry cannot name, and leaves the route
 *        where it was.
 *
 * @details The identifier was cut to 8 bits: 0x100 moved the route to APIC ID 0, and 0xFF made it a
 *          broadcast. The caller learns of a refusal from the value returned: the count is the
 *          scaffold's, which the machine record reports after this test has run.
 */
KERNEL_TEST(moving_a_route_refuses_an_identifier_its_entry_cannot_name)
{
    const int32_t masked_route = interrupt_controllers_find_a_masked_route();

    if (input_output_advanced_programmable_interrupt_controller_get_programmed_route_count() == 0u)
    {
        kernel_test_skip(test, "the boot routed no line through an input/output controller");
        return;
    }
    if (masked_route < 0)
    {
        kernel_test_skip(test, "every route is live, and moving one would steer its interrupts");
        return;
    }

    const uint8_t route_index = (uint8_t) masked_route;
    const uint8_t isa_irq =
        input_output_advanced_programmable_interrupt_controller_get_programmed_route_irq(route_index);
    const uint8_t found =
        input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(route_index);
    const uint32_t elsewhere = (found == INTERRUPT_CONTROLLERS_TEST_ANOTHER_PROCESSOR) ?
                                   INTERRUPT_CONTROLLERS_TEST_YET_ANOTHER_PROCESSOR :
                                   INTERRUPT_CONTROLLERS_TEST_ANOTHER_PROCESSOR;

    const uint8_t moved = interrupt_controllers_move_route(isa_irq, elsewhere);
    const uint8_t after_move =
        input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(route_index);
    const uint32_t refused_before = input_output_advanced_programmable_interrupt_controller_get_destinations_refused();
    const uint8_t broadcast_taken = interrupt_controllers_move_route(isa_irq, INTERRUPT_CONTROLLERS_TEST_BROADCAST);
    const uint8_t after_broadcast =
        input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(route_index);
    const uint8_t past_eight_bits_taken =
        interrupt_controllers_move_route(isa_irq, INTERRUPT_CONTROLLERS_TEST_PAST_EIGHT_BITS);
    const uint8_t after_past_eight_bits =
        input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(route_index);
    const uint32_t refused_after = input_output_advanced_programmable_interrupt_controller_get_destinations_refused();

    (void) interrupt_controllers_move_route(isa_irq, found);

    kernel_test_check(test, moved && after_move == elsewhere, "a route moves to another processor");
    kernel_test_check(test, !broadcast_taken && after_broadcast == elsewhere,
                      "0xFF is refused, and the route stays where it was");
    kernel_test_check(test, !past_eight_bits_taken && after_past_eight_bits == elsewhere, "so is 0x100");
    kernel_test_check(test, refused_after == refused_before,
                      "and neither counts as a line the scaffold refused, which the machine record reports");
    kernel_test_check(test,
                      input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(
                          route_index) == found,
                      "the route is put back where it was found");
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
