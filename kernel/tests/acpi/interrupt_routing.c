#include "fixtures/interrupt_routing.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/interrupt_routing.h>
#include <kernel/acpi/machine_language.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/resources.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_interrupt_routing, KERNEL_TEST_STAGE_INITIALIZATION);

#define ROUTING_TEST_APIC      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_APIC
#define ROUTING_TEST_PIC       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_PIC
#define ROUTING_TEST_STATUS(x) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##x
#define ROUTING_TEST_LEVEL     1u    /**< A route's level field when level-triggered. */
#define ROUTING_TEST_EDGE      0u    /**< When edge-triggered. */
#define ROUTING_TEST_LOW       1u    /**< Its active_low field when active-low. */
#define ROUTING_TEST_HIGH      0u    /**< When active-high. */
#define ROUTING_TEST_TABLE     8192u /**< Bytes of AML the largest made-up table holds, and more. */
#define ROUTING_TEST_UNKNOWN   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_UNKNOWN
#define ROUTING_TEST_NONE      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_NO_INTERRUPT
#define ROUTING_TEST_OFFSET    0x811C9DC5u /**< FNV-1a's offset basis. */
#define ROUTING_TEST_PRIME     0x01000193u /**< FNV-1a's prime. */

typedef AdvancedConfigurationAndPowerInterfaceInterruptRoute_t RoutingTestRoute_t;
typedef AdvancedConfigurationAndPowerInterfaceInterruptRoutingHop_t RoutingTestHop_t;
typedef AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t RoutingTestTable_t;
typedef AdvancedConfigurationAndPowerInterfaceResource_t RoutingTestResource_t;
typedef AdvancedConfigurationAndPowerInterfaceResourceInterrupt_t RoutingTestInterrupt_t;

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t routing_test_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t routing_test_load;
static AdvancedConfigurationAndPowerInterfaceInitialization_t routing_test_counts;
static AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t routing_test_idle;
static RoutingTestTable_t routing_test_table;
static AdvancedConfigurationAndPowerInterfaceInitializationMachine_t routing_test_machine;
static uint8_t routing_test_bytes[MADE_UP_TABLE_HEADER + ROUTING_TEST_TABLE];

/**
 * @brief Loads the made-up table @p aml as a DSDT of revision 2, initialises its namespace in the interrupt model
 *        @p model, then publishes its routes.
 */
static bool routing_test_run(KernelTest_t *test, const uint8_t *aml, uint32_t length, uint8_t model)
{
    memset(&routing_test_machine, 0, sizeof(routing_test_machine));
    routing_test_machine.interrupt_model = model;
    advanced_configuration_and_power_interface_simulated_host_open(&routing_test_host);
    if (!kernel_test_check(test, length <= ROUTING_TEST_TABLE, "the made-up table fits") ||
        !made_up_load_on(test, &routing_test_load, &routing_test_host.host, routing_test_bytes, 2u, aml, length))
        return false;
    advanced_configuration_and_power_interface_initialize_namespace(&routing_test_load, &routing_test_machine,
                                                                    &routing_test_counts, &routing_test_idle);
    advanced_configuration_and_power_interface_interrupt_routing_publish(&routing_test_load, &routing_test_machine,
                                                                         &routing_test_counts, &routing_test_table);
    return true;
}

/**
 * @brief Checks that the evaluations left nothing behind, then closes the load.
 */
static void routing_test_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &routing_test_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && routing_test_load.temporary_count == 0u,
                      "every evaluation leaves no frame, value, operator or temporary node behind");
    kernel_test_check(test, routing_test_load.heap.stale_handles == 0u, "no retain or release met a dead object");
    advanced_configuration_and_power_interface_machine_language_close(&routing_test_load);
    advanced_configuration_and_power_interface_simulated_host_close(&routing_test_host);
}

/**
 * @brief The node of @p name, four characters, below \_SB_, or 0.
 */
static uint32_t routing_test_below_system_bus(const char *name)
{
    const uint32_t bus = advanced_configuration_and_power_interface_machine_language_namespace_find_object(
        &routing_test_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "_SB_", 0u));

    return advanced_configuration_and_power_interface_machine_language_namespace_find_object(
        &routing_test_load.space, bus,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u));
}

/**
 * @brief The Integer the absolute path @p path evaluates to, or 0xFFFFFFFF when it does not.
 */
static uint64_t routing_test_integer(const char *path)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    const bool read = advanced_configuration_and_power_interface_machine_language_evaluate(
                          &routing_test_load, path, NULL, 0u, &result) == ROUTING_TEST_STATUS(OK) &&
                      result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER;
    const uint64_t value = read ? result.payload : 0xFFFFFFFFu;

    advanced_configuration_and_power_interface_machine_language_value_release(&routing_test_load.heap,
                                                                              &routing_test_load.space, &result);
    return value;
}

/**
 * @brief Whether the table routes pin @p pin of device @p device, on bus @p bus of segment @p segment, to
 *        @p interrupt as @p level and @p active_low say, through @p link.
 */
static bool routing_test_routes(uint16_t segment, uint8_t bus, uint8_t device, uint8_t pin, uint32_t interrupt,
                                uint8_t level, uint8_t active_low, uint32_t link)
{
    RoutingTestRoute_t route;

    return advanced_configuration_and_power_interface_interrupt_routing_find(&routing_test_table, segment, bus, NULL,
                                                                             0u, device, pin, &route) &&
           route.interrupt == interrupt && route.level == level && route.active_low == active_low && route.link == link;
}

/**
 * @brief Whether the table holds no route for pin @p pin of device @p device on bus 0 of segment 0.
 */
static bool routing_test_unrouted(uint8_t device, uint8_t pin)
{
    RoutingTestRoute_t route;

    return !advanced_configuration_and_power_interface_interrupt_routing_find(&routing_test_table, 0u, 0u, NULL, 0u,
                                                                              device, pin, &route);
}

/**
 * @brief Whether the table routes pin @p pin of device @p device, on the bus below the @p depth bridges @p addresses
 *        names from the root bridge down, each one's device and function as its _ADR gives them, to @p interrupt,
 *        level-triggered and active-low, wired.
 */
static bool routing_test_routes_below(const uint32_t *addresses, uint32_t depth, uint8_t device, uint8_t pin,
                                      uint32_t interrupt)
{
    for (uint32_t index = 0u; index < routing_test_table.count; ++index)
    {
        const RoutingTestRoute_t *route = &routing_test_table.routes[index];
        uint16_t bridge = route->bridge;
        uint32_t level = depth;

        if (route->device != device || route->pin != pin)
            continue;
        while (bridge != 0u && level != 0u && routing_test_table.below[bridge - 1u].address == addresses[level - 1u])
        {
            bridge = routing_test_table.below[bridge - 1u].above;
            --level;
        }
        if (route->bridge != 0u && bridge == 0u && level == 0u)
            return route->interrupt == interrupt && route->level == ROUTING_TEST_LEVEL &&
                   route->active_low == ROUTING_TEST_LOW && route->link == 0u;
    }
    return false;
}

/**
 * @brief Whether the lookup of pin @p pin of device @p device behind the @p hop_count bridges @p hops, from bus 0 of
 *        segment 0, finds a route to @p interrupt.
 */
static bool routing_test_reaches(const RoutingTestHop_t *hops, uint32_t hop_count, uint8_t device, uint8_t pin,
                                 uint32_t interrupt)
{
    RoutingTestRoute_t route;

    return advanced_configuration_and_power_interface_interrupt_routing_find(&routing_test_table, 0u, 0u, hops,
                                                                             hop_count, device, pin, &route) &&
           route.interrupt == interrupt;
}

/**
 * @brief How many entries of the table give no interrupt.
 */
static uint32_t routing_test_without_interrupt(void)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < routing_test_table.count; ++index)
        count += (routing_test_table.routes[index].interrupt == ROUTING_TEST_NONE) ? 1u : 0u;
    return count;
}

/**
 * @brief A wired pin goes to the Global System Interrupt its SourceIndex gives, level-triggered and active-low, the
 *        first entry of two for the same pin winning, as Linux takes the first match.
 */
KERNEL_TEST(a_wired_pin_goes_to_the_interrupt_its_source_index_gives)
{
    if (!routing_test_run(test, routing_wired_pins_aml, sizeof(routing_wired_pins_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.bridges == 1u && routing_test_table.entries == 4u,
                      "one root bridge, four entries read");
    kernel_test_check(test, routing_test_table.count == 4u && routing_test_table.entries_malformed == 0u,
                      "each entry is published");
    kernel_test_check(test, routing_test_routes(0u, 0u, 3u, 0u, 20u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "device 3 INTA goes to GSI 20, level-triggered, active-low: the first of its two entries");
    kernel_test_check(test, routing_test_routes(0u, 0u, 3u, 1u, 21u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "device 3 INTB goes to GSI 21");
    kernel_test_check(test, routing_test_routes(0u, 0u, 31u, 3u, 23u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "device 31 INTD goes to GSI 23");
    kernel_test_check(test, routing_test_unrouted(3u, 2u), "device 3 INTC, which no entry names, has no route");

    RoutingTestRoute_t route;

    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_interrupt_routing_find(&routing_test_table, 0u, 1u,
                                                                                         NULL, 0u, 3u, 0u, &route) &&
                          !advanced_configuration_and_power_interface_interrupt_routing_find(
                              &routing_test_table, 1u, 0u, NULL, 0u, 3u, 0u, &route),
                      "nor does device 3 INTA on bus 1, or on segment 1");
    kernel_test_measure(test, "routes", routing_test_table.count);
    routing_test_close(test);
}

/**
 * @brief A link gives its current interrupt, read once however many entries name it, with the trigger and polarity
 *        of the first interrupt descriptor of its _PRS, a link that functions without being present included; an
 *        entry whose SourceIndex is not 0 is unrouted; an entry whose Source and SourceIndex are swapped is read as
 *        ACPICA repairs it; the first entry for a pin decides, even when its link gives no interrupt.
 */
KERNEL_TEST(a_link_gives_its_current_interrupt_with_the_signalling_of_its_possible_settings)
{
    if (!routing_test_run(test, routing_links_aml, sizeof(routing_links_aml), ROUTING_TEST_APIC))
        return;

    const uint32_t first = routing_test_below_system_bus("LNKA");
    const uint32_t second = routing_test_below_system_bus("LNKB");

    kernel_test_check(test, first != 0u && second != 0u, "both links are declared");
    kernel_test_check(test, routing_test_routes(0u, 0u, 2u, 0u, 17u, ROUTING_TEST_LEVEL, ROUTING_TEST_HIGH, first),
                      "device 2 INTA goes to LNKA's 17, level-triggered and active-high as its _PRS says, not as its "
                      "_CRS does");
    kernel_test_check(test, routing_test_routes(0u, 0u, 2u, 1u, 17u, ROUTING_TEST_LEVEL, ROUTING_TEST_HIGH, first),
                      "device 2 INTB goes there too");
    kernel_test_check(test, routing_test_routes(0u, 0u, 4u, 0u, 11u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, second),
                      "device 4 INTA goes to LNKB's IRQ 11, level-triggered, active-low");
    kernel_test_check(test, routing_test_unrouted(4u, 1u), "device 4 INTB, whose SourceIndex is 1, is unrouted");
    kernel_test_check(test,
                      routing_test_routes(0u, 0u, 6u, 0u, 10u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW,
                                          routing_test_below_system_bus("LNKJ")),
                      "device 6 INTA goes to LNKJ's IRQ 10, its _STA saying functioning but not present");
    kernel_test_check(test, routing_test_unrouted(7u, 0u),
                      "device 7 INTA has no route: its first entry names LNKU, which gives none, and the entry that "
                      "wires it comes second");
    kernel_test_check(test,
                      routing_test_routes(0u, 0u, 8u, 0u, 11u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, second) &&
                          routing_test_table.entries_repaired == 1u,
                      "device 8 INTA, its Source and SourceIndex swapped, goes to LNKB's IRQ 11, a repair counted");
    kernel_test_check(test, routing_test_table.links_unrouted == 2u && routing_test_without_interrupt() == 2u,
                      "the SourceIndex of 1 and LNKU are unrouted, and their entries published without an interrupt");
    kernel_test_check(test, routing_test_table.links == 7u && routing_test_table.count == 8u,
                      "seven of the eight entries name a link, and all eight are published");
    kernel_test_check(test, routing_test_integer("\\CRSN") == 1u, "LNKA's _CRS runs once for its three entries");
    routing_test_close(test);
}

/**
 * @brief A link routes nothing unless it is present, is a link, and its _CRS gives an interrupt other than 0 its _PRS
 *        lists; an _STA or a _CRS that fails, a _CRS that gives no Buffer, or a template ACPICA refuses, is counted as
 * a failure under its status; each entry is still published, without an interrupt.
 */
KERNEL_TEST(a_link_routes_nothing_unless_its_current_interrupt_is_one_it_may_take)
{
    if (!routing_test_run(test, routing_unrouted_links_aml, sizeof(routing_unrouted_links_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.entries == 14u && routing_test_table.links == 14u,
                      "fourteen entries, each naming a device");
    kernel_test_check(test, routing_test_table.links_unrouted == 7u,
                      "an absent link, an IRQ its _PRS lacks, an empty IRQ, no _PRS, no _CRS, IRQ 0 and no link route "
                      "nothing");
    kernel_test_check(test, routing_test_table.links_failed == 7u,
                      "a _CRS that returns an Integer, one that fails, an _STA that fails, and a _CRS whose descriptor "
                      "runs past its bytes, one without an End Tag, one with an IRQ of one byte and one with an "
                      "Extended Interrupt that names none are failures");
    kernel_test_check(test,
                      routing_test_counts.statuses[ROUTING_TEST_STATUS(TYPE_MISMATCH)] == 5u &&
                          routing_test_counts.statuses[ROUTING_TEST_STATUS(INDEX_OUT_OF_RANGE)] == 2u,
                      "each failure is counted once, under its status");
    kernel_test_check(test, routing_test_table.count == 14u && routing_test_without_interrupt() == 14u,
                      "every entry is published, without an interrupt");
    kernel_test_check(
        test, routing_test_unrouted(10u, 0u) && routing_test_unrouted(13u, 0u) && routing_test_unrouted(14u, 0u),
        "devices 10, 13 and 14 have no route, though LNKL's _CRS starts with an IRQ 11 its _PRS lists, "
        "and LNKO's and LNKP's hold one");
    routing_test_close(test);
}

/**
 * @brief The 65th link an entry names comes past the ones a boot resolves: it is counted as dropped, not as a link that
 *        routes nothing, and its entry is published without an interrupt.
 */
KERNEL_TEST(a_link_past_the_ones_a_boot_resolves_is_dropped)
{
    if (!routing_test_run(test, routing_many_links_aml, sizeof(routing_many_links_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.links == 65u && routing_test_table.count == 65u,
                      "65 entries name a Device, and all are published");
    kernel_test_check(test, routing_test_table.links_unrouted == 64u, "the 64 Devices that are no link route nothing");
    kernel_test_check(test,
                      routing_test_table.links_dropped == 1u && routing_test_table.links_failed == 0u &&
                          routing_test_unrouted(16u, 0u),
                      "LNKZ, the 65th, is dropped, and device 16 INTA has no route");
    routing_test_close(test);
}

/**
 * @brief An entry that is no Package of four Integers and a Source, or names a device past 31 or a pin past 3, is left
 *        out and counted; the entry beside them is published, from a _PRT method.
 */
KERNEL_TEST(a_malformed_entry_is_left_out_and_counted)
{
    if (!routing_test_run(test, routing_malformed_entries_aml, sizeof(routing_malformed_entries_aml),
                          ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.entries == 8u && routing_test_table.entries_malformed == 7u,
                      "seven of the eight entries are malformed");
    kernel_test_check(test,
                      routing_test_table.count == 1u &&
                          routing_test_routes(0u, 0u, 1u, 0u, 16u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "the eighth routes device 1 INTA to GSI 16");
    kernel_test_check(test, routing_test_table.links == 0u, "no malformed entry is read as a link");
    routing_test_close(test);
}

/**
 * @brief Only a Device whose _HID or _CID names PNP0A03 is a root bridge, as Linux binds its PCI root driver; its _PRT
 *        is read when _STA, its own and that of each Device above it, says present or functioning, and when its _SEG
 *        and _BBN, which give the segment and bus, do not fail. Each failure is counted once, under its status.
 */
KERNEL_TEST(only_the_root_bridges_present_by_their_status_are_read)
{
    if (!routing_test_run(test, routing_bridges_aml, sizeof(routing_bridges_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.bridges == 9u,
                      "nine root bridges: not BRG0, not PCI3, a PCI Express root complex alone, not DEV2");
    kernel_test_check(test,
                      routing_test_table.bridges_absent == 3u && routing_test_unrouted(1u, 0u) &&
                          routing_test_unrouted(8u, 0u) && routing_test_unrouted(11u, 0u),
                      "PCI0 absent, PCI4 whose _STA fails and PCI8 below an absent device are counted, their _PRT not "
                      "read");
    kernel_test_check(test,
                      routing_test_table.bridges_failed == 2u && routing_test_unrouted(9u, 0u) &&
                          routing_test_unrouted(10u, 0u),
                      "PCI6, whose _BBN fails, and PCI7, whose _SEG fails, are counted, their _PRT not read");
    kernel_test_check(test, routing_test_table.bridges_unrouted == 1u, "PCI1, without a _PRT, is counted");
    kernel_test_check(test, routing_test_table.tables_failed == 1u, "PCI9's _PRT, an Integer, is a failed table");
    kernel_test_check(test, routing_test_routes(1u, 2u, 5u, 0u, 40u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "PCI2's one entry routes device 5 INTA on segment 1, bus 2, to GSI 40");
    kernel_test_check(test, routing_test_routes(0u, 0u, 6u, 0u, 41u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "PCI5, functioning but not present, routes device 6 INTA to GSI 41");
    kernel_test_check(test,
                      routing_test_table.bridges_below == 1u && routing_test_table.below[0].segment == 1u &&
                          routing_test_table.below[0].bus == 2u && routing_test_table.routes[2].bridge == 1u &&
                          routing_test_table.routes[2].segment == 1u && routing_test_table.routes[2].bus == 2u &&
                          routing_test_routes_below((const uint32_t[]){0x00050000u}, 1u, 0u, 0u, 41u),
                      "BRG0, a bridge below PCI2 by its _ADR, routes device 0 INTA of the bus below 05.0 to GSI 41, "
                      "its entry published after those of PCI2 and PCI5, the two root bridges read");
    kernel_test_check(test,
                      routing_test_table.count == 3u && routing_test_unrouted(7u, 0u) && routing_test_unrouted(12u, 0u),
                      "nothing else is published: not PCI3's nor DEV2's _PRT");
    kernel_test_check(test,
                      routing_test_counts.statuses[ROUTING_TEST_STATUS(INDEX_OUT_OF_RANGE)] == 4u &&
                          routing_test_counts.statuses[ROUTING_TEST_STATUS(TYPE_MISMATCH)] == 1u,
                      "PCI4's _STA, PCI6's _BBN, PCI7's _SEG and DEV2's _HID fail once each, PCI9's _PRT has the "
                      "wrong type");
    routing_test_close(test);
}

/**
 * @brief Below a root bridge, a bridge is the Device Linux takes as a PCI bridge's companion: a child of the root
 *        bridge or of the bridge above it whose _ADR names a PCI function; of several that name the same one, the
 *        first scored best; never one right below the root bridge whose _ADR is 0 and that has a _HID. One that holds
 *        a _PRT or sits above one is published, up to eight below the root bridge, and its entries route the devices
 *        of its secondary bus.
 */
KERNEL_TEST(the_bridges_below_a_root_bridge_are_the_companions_linux_takes)
{
    if (!routing_test_run(test, routing_bridges_below_aml, sizeof(routing_bridges_below_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_routes(0u, 0u, 1u, 0u, 16u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "the root bridge routes device 1 INTA of the root bus to GSI 16");
    kernel_test_check(test,
                      routing_test_routes_below((const uint32_t[]){0x001C0000u}, 1u, 0u, 0u, 17u) &&
                          routing_test_routes_below((const uint32_t[]){0x001C0000u}, 1u, 0u, 3u, 18u),
                      "RP01, 1C.0, routes INTA and INTD of the bus below it to GSI 17 and 18");
    kernel_test_check(test,
                      routing_test_routes_below((const uint32_t[]){0x001C0000u, 0u, 0x00010000u}, 3u, 0u, 1u, 19u),
                      "DSP0, 01.0 below PXSX, 00.0 below RP01, routes INTB to GSI 19");
    kernel_test_check(test,
                      routing_test_routes_below((const uint32_t[]){0x001C0001u}, 1u, 0u, 0u, 20u) &&
                          routing_test_routes_below((const uint32_t[]){0x001C0001u, 0u}, 2u, 0u, 0u, 21u),
                      "RP02, 1C.1 by its _ADR method, routes GSI 20, and HIDB below it 21: a _HID keeps out 00.0 only "
                      "right below the root bridge");
    kernel_test_check(test,
                      routing_test_routes_below((const uint32_t[]){0x001C0002u}, 1u, 0u, 0u, 22u) &&
                          routing_test_integer("\\CNTU") == 0u,
                      "of 1C.2, DUPB, enabled without a _HID, scores high and routes GSI 22: DUPA, without a child, is "
                      "refused, and DUPC after it is not scored");
    kernel_test_check(test, routing_test_routes_below((const uint32_t[]){0x00020000u}, 1u, 0u, 0u, 23u),
                      "of 02.0, VIDA, a video device by its _DOS without _STA whose child is a PowerResource, routes "
                      "GSI 23, past VIDB, enabled with a _HID, and VIDC, not enabled");
    kernel_test_check(test,
                      routing_test_routes_below((const uint32_t[]){0x00060000u}, 1u, 0u, 0u, 24u) &&
                          routing_test_integer("\\CNTT") == 0u,
                      "of 06.0, TZA0, whose child is a ThermalZone, scores high and routes GSI 24: TZB0 is not scored, "
                      "its _STA never run");
    kernel_test_check(test, routing_test_routes_below((const uint32_t[]){0x00070000u}, 1u, 0u, 0u, 25u),
                      "of 07.0, PRA0, whose child is a Processor, routes GSI 25");
    kernel_test_check(test, routing_test_routes_below((const uint32_t[]){0x00080000u}, 1u, 0u, 0u, 26u),
                      "of 08.0, R080, a video device by its _ROM, routes GSI 26, past L080 and its EisaId _HID");
    kernel_test_check(test,
                      routing_test_routes_below((const uint32_t[]){0x00090000u}, 1u, 0u, 0u, 27u) &&
                          routing_test_routes_below((const uint32_t[]){0x000A0000u}, 1u, 0u, 0u, 28u),
                      "a _VPO, a _GPD and a _SPD make P090 a video device, which routes GSI 27; NA10 to NA30, each "
                      "without one of them, do not pass L0A0, which routes 28");
    kernel_test_check(test, routing_test_routes_below((const uint32_t[]){0x000B0000u}, 1u, 0u, 0u, 29u),
                      "of 0B.0, D0B0, a video device by its _DOD, routes GSI 29");
    kernel_test_check(test, routing_test_routes_below((const uint32_t[]){0x000E0000u}, 1u, 0u, 0u, 30u),
                      "of 0E.0, NFB0, whose _STA does not find \\MISS, scores as a Device without _STA and routes GSI "
                      "30, past NFA0, whose _STA fails otherwise and refuses it");
    kernel_test_check(test, routing_test_integer("\\CNTS") == 0u,
                      "DSP0, the one child of PXSX that names 01.0, is not scored: its _STA never runs");
    kernel_test_check(test, routing_test_table.count == 15u && routing_test_unrouted(0u, 0u),
                      "nothing else is published: none of the Devices scored lower, HIDD, ALLF, BIGD, BADA, NOAD, "
                      "TZHD nor DP09, and no entry of a bridge is the root bus's");
    kernel_test_check(test,
                      routing_test_table.bridges_below == 22u && routing_test_table.bridges_below_dropped == 1u &&
                          routing_test_table.depth_peak ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_DEPTH,
                      "22 bridges are published, DEEP and the seven below it among them, eight deep; DP09, the ninth "
                      "below the root bridge, is dropped");
    kernel_test_check(test, routing_test_table.addresses_shared == 9u && routing_test_table.children_peak == 28u,
                      "nine functions are each named by several children, 28 of the root bridge's children name a "
                      "function");
    kernel_test_check(
        test,
        routing_test_counts.statuses[ROUTING_TEST_STATUS(INDEX_OUT_OF_RANGE)] == 2u &&
            routing_test_table.tables_failed == 0u,
        "BADA's _ADR and NFA0's _STA each fail once, counted, and no bridge without a _PRT is read as one that "
        "fails");
    routing_test_close(test);
}

/**
 * @brief The children of a bridge are read only when a Device among them holds a _PRT or sits above one that does,
 *        and the children that name one function are scored only when one of them does: no bridge routes anything
 *        otherwise.
 */
KERNEL_TEST(the_bridges_below_are_read_only_where_a_table_lies_below)
{
    if (!routing_test_run(test, routing_bridges_read_aml, sizeof(routing_bridges_read_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test,
                      routing_test_routes_below((const uint32_t[]){0x001C0000u}, 1u, 0u, 0u, 16u) &&
                          routing_test_table.count == 1u && routing_test_table.bridges_below == 1u,
                      "RP01 alone is published, and routes GSI 16");
    kernel_test_check(test, routing_test_integer("\\CNTR") == 0u,
                      "RP01, alone to name 1C.0, is not scored, though it has a child: its _STA never runs");
    kernel_test_check(test, routing_test_integer("\\CNTA") == 0u,
                      "ENDP, the one Device child of RP01, holds no _PRT, and TZD0's lies in RP01's ThermalZone: "
                      "RP01's children are not read, ENDP's _ADR never runs");
    kernel_test_check(test, routing_test_integer("\\CNTB") == 0u,
                      "nothing below PCI1 holds a _PRT: its children are not read, EP10's _ADR never runs");
    kernel_test_check(test, routing_test_integer("\\CNTG") == 0u && routing_test_table.addresses_shared == 2u,
                      "GRA0 and GRB0 both name 02.0 and hold no _PRT: they are counted among the functions several "
                      "children name, with 03.0, and never scored");
    kernel_test_check(test, routing_test_integer("\\CNTH") == 1u,
                      "HST0, 00.0 right below the root bridge, holds no _PRT: its _HID runs once, as the search for "
                      "root bridges runs it, and not again for the rule on 00.0");
    kernel_test_check(test, routing_test_table.children_peak == 6u,
                      "the six children of PCI0 that name a function are compared");
    routing_test_close(test);
}

/**
 * @brief A function behind bridges takes the entry Linux finds on the way up: the first one of the _PRT of the bridge
 *        above its bus for its device and pin; without one, its pin swizzled through that bridge, (pin + device) mod
 *        4, or replaced by a CardBus bridge's own, for the bridge's device on the bus above, up to the root bridge.
 *        An entry found ends the lookup, even one whose link gives no interrupt.
 */
KERNEL_TEST(a_function_behind_bridges_takes_the_entry_linux_finds_on_the_way_up)
{
    static const RoutingTestHop_t root_port[] = {{.device = 0x1Cu}};
    static const RoutingTestHop_t downstream[] = {{.device = 0x1Cu}, {.device = 0u}, {.device = 2u}};
    static const RoutingTestHop_t linked[] = {{.device = 0x1Du}};
    static const RoutingTestHop_t below_linked[] = {{.device = 0x1Du}, {.device = 0u}};
    static const RoutingTestHop_t undescribed[] = {{.device = 5u}};
    static const RoutingTestHop_t other_function[] = {
        {.device = 0x1Cu, .function = 1u},
        {.device = 0u},
        {.device = 2u}
    };
    static const RoutingTestHop_t cardbus[] = {
        {.device = 0x1Cu, .cardbus = true, .pin = 2u}
    };
    static const RoutingTestHop_t pinless_cardbus[] = {
        {.device = 0x1Cu},
        {.device = 0u, .cardbus = true, .pin = 0u}
    };
    static const RoutingTestHop_t gone_cardbus[] = {
        {.device = 0x1Cu},
        {.device = 0u, .cardbus = true, .pin = 0xFFu}
    };
    RoutingTestRoute_t route;

    if (!routing_test_run(test, routing_swizzle_aml, sizeof(routing_swizzle_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_reaches(NULL, 0u, 0x1Du, 0u, 20u),
                      "device 0x1D INTA on the root bus takes the root bridge's entry, GSI 20");
    kernel_test_check(test, routing_test_reaches(root_port, 1u, 3u, 1u, 16u),
                      "behind RP01, which has no _PRT, device 3 INTB swizzles to INTA of device 0x1C: GSI 16");
    kernel_test_check(test, routing_test_reaches(downstream, 3u, 0u, 0u, 24u),
                      "behind DSP2, device 0 INTA takes DSP2's own entry, GSI 24");
    kernel_test_check(test, routing_test_reaches(downstream, 3u, 0u, 1u, 19u),
                      "device 0 INTB, which DSP2's _PRT does not name, swizzles to INTB of DSP2's device 2, INTD of "
                      "PXSX's device 0, then INTD of device 0x1C: GSI 19");
    kernel_test_check(test, routing_test_reaches(other_function, 3u, 0u, 0u, 18u),
                      "behind function 1 of device 0x1C, which no bridge stands for, the pin swizzles from the "
                      "bottom: INTA, INTC, then INTC of device 0x1C, GSI 18");
    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_interrupt_routing_find(&routing_test_table, 0u, 0u,
                                                                                         linked, 1u, 0u, 0u, &route) &&
                          route.interrupt == ROUTING_TEST_NONE,
                      "behind RP05, device 0 INTA's entry names LNKZ, which gives no interrupt, and ends the lookup: "
                      "GSI 20 of device 0x1D INTA is not taken");
    kernel_test_check(test, routing_test_reaches(linked, 1u, 1u, 0u, 21u),
                      "device 1 INTA, which RP05's _PRT does not name, swizzles to INTB of device 0x1D: GSI 21");
    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_interrupt_routing_find(
                          &routing_test_table, 0u, 0u, undescribed, 1u, 0u, 0u, &route),
                      "behind device 5, which the root bridge's _PRT does not name, there is no route");
    kernel_test_check(test, routing_test_reaches(cardbus, 1u, 2u, 0u, 17u),
                      "behind a CardBus bridge at 0x1C whose pin is INTB, device 2 INTA takes INTB, where a swizzle "
                      "would give INTC: GSI 17");
    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_interrupt_routing_find(
                          &routing_test_table, 0u, 0u, pinless_cardbus, 2u, 2u, 0u, &route),
                      "behind a CardBus bridge without a pin, below RP01, there is no route, where swizzling on would "
                      "reach device 0x1C");
    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_interrupt_routing_find(
                          &routing_test_table, 0u, 0u, gone_cardbus, 2u, 2u, 0u, &route),
                      "behind a CardBus bridge whose Interrupt Pin reads 0xFF, a reserved value, there is no route, "
                      "where swizzling it on would reach GSI 18 of device 0x1C");
    kernel_test_check(test, routing_test_reaches(below_linked, 2u, 0u, 0u, 25u),
                      "behind RPXS, 00.0 below RP05, device 0 INTA takes RPXS's entry, GSI 25, though PXSX, 00.0 "
                      "below RP01, is published before it");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_interrupt_routing_find(
                          &routing_test_table, 0u, 1u, root_port, 1u, 0u, 0u, &route) &&
                          route.interrupt == 26u &&
                          advanced_configuration_and_power_interface_interrupt_routing_find(
                              &routing_test_table, 1u, 0u, root_port, 1u, 0u, 0u, &route) &&
                          route.interrupt == 27u,
                      "behind 1C.0 of bus 1, RQ01 below PCI1 routes device 0 INTA to GSI 26, and behind 1C.0 of "
                      "segment 1, RS01 below PCI2 to 27, where RP01, 1C.0 of bus 0 of segment 0, has no _PRT");
    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_interrupt_routing_find(
                          &routing_test_table, 0u, 1u, downstream, 3u, 0u, 0u, &route) &&
                          !advanced_configuration_and_power_interface_interrupt_routing_find(
                              &routing_test_table, 1u, 0u, downstream, 3u, 0u, 0u, &route),
                      "from bus 1, and from segment 1, behind the bridges that lead to DSP2 on bus 0, the pin swizzles "
                      "up to INTC of RQ01 and RS01, which name INTA alone, and to PCI1 and PCI2, which have no _PRT: "
                      "there is no route, DSP2's entry and PCI0's not taken");
    routing_test_close(test);
}

/**
 * @brief A bridge past the 64 a boot publishes, and a child past the 256 of one bridge whose _ADR is compared, are
 *        counted as dropped.
 */
KERNEL_TEST(the_bridges_past_the_ones_a_boot_publishes_are_dropped)
{
    if (!routing_test_run(test, routing_many_bridges_aml, sizeof(routing_many_bridges_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.children_peak == 256u,
                      "256 children of the root bridge are compared, one for each function of its bus");
    kernel_test_check(test,
                      routing_test_table.bridges_below ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_BRIDGES &&
                          routing_test_table.bridges_below_dropped == 2u,
                      "64 bridges are published; B040, the 65th, and B100, the 257th child, are dropped");
    kernel_test_check(test, routing_test_table.count == 0u && routing_test_table.addresses_shared == 0u,
                      "their _PRT are empty, and B100 is not compared with B000");
    routing_test_close(test);
}

/**
 * @brief Every root bridge's own _PRT is read before the bridges below any of them, so that the bridges below one never
 *        crowd another's routes out of the table.
 */
KERNEL_TEST(the_root_bridges_routes_come_before_those_of_the_bridges_below_them)
{
    if (!routing_test_run(test, routing_roots_first_aml, sizeof(routing_roots_first_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_routes(0u, 1u, 3u, 0u, 16u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "PCI1, read before the 64 bridges below PCI0, routes device 3 INTA of bus 1 to GSI 16");
    kernel_test_check(test,
                      routing_test_table.count ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_ROUTES &&
                          routing_test_table.entries == 257u && routing_test_table.entries_dropped == 1u &&
                          routing_test_table.bridges_below == 64u,
                      "of the 257 entries, the last one the 64th bridge below PCI0 holds is dropped");
    kernel_test_check(test,
                      routing_test_routes_below((const uint32_t[]){0x00070006u}, 1u, 0u, 2u, 34u) &&
                          !routing_test_routes_below((const uint32_t[]){0x00070007u}, 1u, 0u, 3u, 35u),
                      "BR3E, 07.6, still routes INTC to GSI 34, and BR3F, 07.7, its INTD no more");
    routing_test_close(test);
}

/**
 * @brief The bridges below the root bridges past the first 16 a boot reads are not read, those root bridges counted,
 *        their own _PRT read.
 */
KERNEL_TEST(the_bridges_below_root_bridges_past_the_ones_a_boot_walks_are_not_read)
{
    if (!routing_test_run(test, routing_many_roots_aml, sizeof(routing_many_roots_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test,
                      routing_test_table.bridges == 17u && routing_test_table.bridges_unwalked == 1u &&
                          routing_test_table.bridges_below == 1u && routing_test_table.bridges_below_dropped == 0u,
                      "of 17 root bridges, RB10, the 17th, is counted, and the one bridge published is RB0F's");
    kernel_test_check(test,
                      routing_test_routes(0u, 15u, 1u, 0u, 31u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u) &&
                          routing_test_routes(0u, 16u, 1u, 0u, 32u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "RB0F and RB10 route device 1 INTA of their buses to GSI 31 and 32 in their own _PRT");
    kernel_test_check(test,
                      routing_test_table.count == 18u && routing_test_table.below[0].bus == 15u &&
                          routing_test_routes_below((const uint32_t[]){0x001C0000u}, 1u, 0u, 0u, 79u),
                      "below RB0F, P1C0 routes GSI 79; RB10's P1C0 is not read");
    routing_test_close(test);
}

/**
 * @brief In the APIC model step 5 calls \_PIC (1) before the routes are read, so QEMU's pattern wires the pin to a
 *        GSI; in the PIC model \_PIC is not called, and the same pin goes to the link's ISA IRQ (§3.8 step 5).
 */
KERNEL_TEST(the_apic_model_tells_pic_one_before_the_routes_are_read)
{
    if (!routing_test_run(test, routing_interrupt_model_aml, sizeof(routing_interrupt_model_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test,
                      routing_test_counts.pic_mode_told == 1u && routing_test_counts.pic_mode_deferred == 0u &&
                          routing_test_integer("\\PICF") == 1u,
                      "the APIC model calls \\_PIC (1), which keeps it");
    kernel_test_check(
        test, !advanced_configuration_and_power_interface_interrupt_routing_leaves_the_8259(&routing_test_table),
        "a PCI function no longer takes its 8259 line");
    kernel_test_check(test,
                      routing_test_table.model == ROUTING_TEST_APIC &&
                          routing_test_routes(0u, 0u, 2u, 0u, 22u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "device 2 INTA is wired to GSI 22");
    routing_test_close(test);

    if (!routing_test_run(test, routing_interrupt_model_aml, sizeof(routing_interrupt_model_aml), ROUTING_TEST_PIC))
        return;
    kernel_test_check(test,
                      routing_test_counts.pic_mode_told == 0u && routing_test_counts.pic_mode_deferred == 1u &&
                          routing_test_integer("\\PICF") == 0u,
                      "the PIC model leaves \\_PIC uncalled, and counts that");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_interrupt_routing_leaves_the_8259(&routing_test_table),
                      "a PCI function still takes its 8259 line");
    kernel_test_check(test,
                      routing_test_table.model == ROUTING_TEST_PIC &&
                          routing_test_routes(0u, 0u, 2u, 0u, 10u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW,
                                              routing_test_below_system_bus("LNKA")),
                      "device 2 INTA goes to LNKA's IRQ 10");
    routing_test_close(test);
}

/**
 * @brief A \_PIC (1) that fails is counted under its status, nothing says the APIC model was taken, and the routes
 *        answer for no known model.
 */
KERNEL_TEST(an_interrupt_model_method_that_fails_is_counted)
{
    if (!routing_test_run(test, routing_failing_interrupt_model_aml, sizeof(routing_failing_interrupt_model_aml),
                          ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_counts.pic_mode_failed == 1u && routing_test_counts.pic_mode_told == 0u,
                      "the failure is counted, the model not taken");
    kernel_test_check(test, routing_test_counts.statuses[ROUTING_TEST_STATUS(INDEX_OUT_OF_RANGE)] == 1u,
                      "under the status it failed with");
    kernel_test_check(test, routing_test_table.model == ROUTING_TEST_UNKNOWN,
                      "the routes answer for an unknown model, firmware having been told none");
    kernel_test_check(
        test, !advanced_configuration_and_power_interface_interrupt_routing_leaves_the_8259(&routing_test_table),
        "so no PCI function takes its 8259 line");
    routing_test_close(test);
}

/**
 * @brief Once a boot's routes are published, the entries after them are counted and not read.
 */
KERNEL_TEST(the_entries_past_a_full_table_are_counted_not_read)
{
    if (!routing_test_run(test, routing_full_table_aml, sizeof(routing_full_table_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.entries == 257u, "257 entries are read");
    kernel_test_check(test,
                      routing_test_table.count ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_ROUTES &&
                          routing_test_table.entries_dropped == 1u,
                      "256 are published, the last one counted as dropped");
    routing_test_close(test);
}

/**
 * @brief FNV-1a over @p count bytes, from the offset basis, written apart from the code under test.
 */
static uint32_t routing_test_fnv(const uint8_t *bytes, uint32_t count)
{
    uint32_t fold = ROUTING_TEST_OFFSET;

    for (uint32_t index = 0u; index < count; ++index)
        fold = (fold ^ bytes[index]) * ROUTING_TEST_PRIME;
    return fold;
}

/**
 * @brief The fold of a table of two routes is FNV-1a over its model, its count as four bytes, then each route's
 *        interrupt as four bytes, segment as two, bus, the count of bridges from its root bridge down to the one
 *        whose _PRT holds it then each one's address as four bytes, device, pin, trigger and polarity,
 *        little-endian; the link is left out, and changing any other field changes the fold.
 */
KERNEL_TEST(the_fold_covers_every_published_field)
{
    static RoutingTestTable_t table;
    const uint8_t expected[] = {
        0x01u, 0x02u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x03u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0x01u, 0x00u, 0x00u, 0x02u, 0x00u, 0x04u, 0x02u, 0x00u,
        0x00u, 0x1Cu, 0x00u, 0x02u, 0x00u, 0x01u, 0x00u, 0x05u, 0x03u, 0x00u, 0x00u,
    };

    memset(&table, 0, sizeof(table));
    table.model = ROUTING_TEST_APIC;
    table.count = 2u;
    table.bridges_below = 2u;
    table.below[0] = (AdvancedConfigurationAndPowerInterfaceInterruptRoutingBridge_t){.address = 0x001C0000u};
    table.below[1] =
        (AdvancedConfigurationAndPowerInterfaceInterruptRoutingBridge_t){.address = 0x00010002u, .above = 1u};
    table.routes[0] = (RoutingTestRoute_t){.interrupt = 16u, .device = 3u, .level = 1u, .active_low = 1u, .link = 7u};
    table.routes[1] =
        (RoutingTestRoute_t){.interrupt = 0x10Bu, .segment = 2u, .bridge = 2u, .bus = 4u, .device = 5u, .pin = 3u};

    const uint32_t fold = advanced_configuration_and_power_interface_interrupt_routing_fold(&table);

    kernel_test_check(test, fold == routing_test_fnv(expected, sizeof(expected)),
                      "the fold is FNV-1a over the published fields in their order");
    table.routes[0].link = 9u;
    kernel_test_check(test, advanced_configuration_and_power_interface_interrupt_routing_fold(&table) == fold,
                      "the link's node is left out");
    table.routes[1].pin = 2u;
    kernel_test_check(test, advanced_configuration_and_power_interface_interrupt_routing_fold(&table) != fold,
                      "a pin changed by one changes it");
    table.routes[1].pin = 3u;
    table.routes[0].active_low = 0u;
    kernel_test_check(test, advanced_configuration_and_power_interface_interrupt_routing_fold(&table) != fold,
                      "a polarity changed changes it");
    table.routes[0].active_low = 1u;
    table.below[0].address = 0x001C0001u;
    kernel_test_check(test, advanced_configuration_and_power_interface_interrupt_routing_fold(&table) != fold,
                      "a bridge's function changed changes it");
    kernel_test_measure_hexadecimal(test, "fold", fold);
}

/**
 * @brief Decodes the descriptor @p bytes holds, before its End Tag, into @p out_resource.
 */
static bool routing_test_descriptor(const uint8_t *bytes, uint32_t length, RoutingTestResource_t *out_resource)
{
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t iterator;

    advanced_configuration_and_power_interface_resources_begin(&iterator, bytes, length);
    return advanced_configuration_and_power_interface_resources_next(&iterator, out_resource) ==
           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_DESCRIPTOR;
}

/**
 * @brief Whether interrupt @p index of @p resource is @p number with @p level and @p active_low.
 */
static bool routing_test_names(const RoutingTestResource_t *resource, uint32_t index, uint32_t number, bool level,
                               bool active_low)
{
    RoutingTestInterrupt_t interrupt;

    return advanced_configuration_and_power_interface_resources_interrupt(resource, index, &interrupt) &&
           interrupt.number == number && interrupt.level == level && interrupt.active_low == active_low;
}

/**
 * @brief An IRQ descriptor names the bits of its mask, lowest first, edge-triggered and active-high without its
 *        information byte (ACPI 6.6 §6.4.2.1); an Extended Interrupt descriptor names each number of its table
 *        (§6.4.3.6); a descriptor too short, another kind, or an index past the last names nothing.
 */
KERNEL_TEST(an_interrupt_descriptor_names_its_interrupts_and_how_they_signal)
{
    static const uint8_t short_irq[] = {0x22u, 0x00u, 0x0Cu, 0x79u, 0x00u};
    static const uint8_t full_irq[] = {0x23u, 0x20u, 0x00u, 0x18u, 0x79u, 0x00u};
    static const uint8_t exclusive_low[] = {0x23u, 0x00u, 0x02u, 0x08u, 0x79u, 0x00u};
    static const uint8_t shared_high[] = {0x23u, 0x00u, 0x02u, 0x10u, 0x79u, 0x00u};
    static const uint8_t extended[] = {0x89u, 0x0Au, 0x00u, 0x07u, 0x02u, 0x11u, 0x00u,
                                       0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x79u};
    static const uint8_t extended_level_low[] = {0x89u, 0x06u, 0x00u, 0x05u, 0x01u, 0x14u, 0x00u, 0x00u, 0x00u, 0x79u};
    static const uint8_t extended_edge_high[] = {0x89u, 0x06u, 0x00u, 0x03u, 0x01u, 0x15u, 0x00u, 0x00u, 0x00u, 0x79u};
    static const uint8_t truncated[] = {0x89u, 0x06u, 0x00u, 0x09u, 0x02u, 0x11u, 0x00u, 0x00u, 0x00u};
    static const uint8_t register_descriptor[] = {0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x01u, 0x02u, 0x01u, 0x00u,
                                                  0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    RoutingTestResource_t resource;
    RoutingTestInterrupt_t interrupt;

    kernel_test_check(test,
                      routing_test_descriptor(short_irq, sizeof(short_irq), &resource) &&
                          routing_test_names(&resource, 0u, 10u, false, false) &&
                          routing_test_names(&resource, 1u, 11u, false, false) &&
                          !advanced_configuration_and_power_interface_resources_interrupt(&resource, 2u, &interrupt),
                      "IRQNoFlags () {10, 11}: 10 then 11, edge-triggered, active-high, nothing third");
    kernel_test_check(test,
                      routing_test_descriptor(full_irq, sizeof(full_irq), &resource) &&
                          routing_test_names(&resource, 0u, 5u, true, true),
                      "IRQ (Level, ActiveLow, Shared) {5}: level-triggered, active-low");
    kernel_test_check(test,
                      routing_test_descriptor(exclusive_low, sizeof(exclusive_low), &resource) &&
                          routing_test_names(&resource, 0u, 9u, true, true) &&
                          routing_test_descriptor(shared_high, sizeof(shared_high), &resource) &&
                          routing_test_names(&resource, 0u, 9u, true, false),
                      "the polarity is bit 3 alone: IRQ (Level, ActiveLow, Exclusive) {9} is active-low, IRQ (Level, "
                      "ActiveHigh, Shared) {9} active-high");
    kernel_test_check(test,
                      routing_test_descriptor(extended, sizeof(extended), &resource) &&
                          routing_test_names(&resource, 0u, 17u, false, true) &&
                          routing_test_names(&resource, 1u, 0x100u, false, true) &&
                          !advanced_configuration_and_power_interface_resources_interrupt(&resource, 2u, &interrupt),
                      "Interrupt (Edge, ActiveLow) {17, 256}: both numbers, nothing third");
    kernel_test_check(test,
                      routing_test_descriptor(extended_level_low, sizeof(extended_level_low), &resource) &&
                          routing_test_names(&resource, 0u, 20u, true, true) &&
                          routing_test_descriptor(extended_edge_high, sizeof(extended_edge_high), &resource) &&
                          routing_test_names(&resource, 0u, 21u, false, false),
                      "the trigger is bit 1 and the polarity bit 2: Interrupt (Level, ActiveLow) {20} and Interrupt "
                      "(Edge, ActiveHigh) {21}");
    kernel_test_check(test,
                      routing_test_descriptor(truncated, sizeof(truncated), &resource) &&
                          !advanced_configuration_and_power_interface_resources_interrupt(&resource, 0u, &interrupt),
                      "an Extended Interrupt whose table runs past its length names nothing");
    kernel_test_check(test,
                      routing_test_descriptor(register_descriptor, sizeof(register_descriptor), &resource) &&
                          !advanced_configuration_and_power_interface_resources_interrupt(&resource, 0u, &interrupt),
                      "a Generic Register names nothing");
}
