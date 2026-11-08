#include "fixtures/interrupt_routing.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/interrupt_routing.h>
#include <kernel/acpi/machine_language.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/resources.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_interrupt_routing, KERNEL_TEST_STAGE_INITIALIZATION);

#define ROUTING_TEST_APIC      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_APIC
#define ROUTING_TEST_PIC       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_PIC
#define ROUTING_TEST_STATUS(x) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##x
#define ROUTING_TEST_LEVEL     1u          /**< A route's level field when level-triggered. */
#define ROUTING_TEST_EDGE      0u          /**< When edge-triggered. */
#define ROUTING_TEST_LOW       1u          /**< Its active_low field when active-low. */
#define ROUTING_TEST_HIGH      0u          /**< When active-high. */
#define ROUTING_TEST_TABLE     1024u       /**< Bytes of AML the largest made-up table holds, and more. */
#define ROUTING_TEST_OFFSET    0x811C9DC5u /**< FNV-1a's offset basis. */
#define ROUTING_TEST_PRIME     0x01000193u /**< FNV-1a's prime. */

typedef AdvancedConfigurationAndPowerInterfaceInterruptRoute_t RoutingTestRoute_t;
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

    return advanced_configuration_and_power_interface_interrupt_routing_find(&routing_test_table, segment, bus, device,
                                                                             pin, &route) &&
           route.interrupt == interrupt && route.level == level && route.active_low == active_low && route.link == link;
}

/**
 * @brief Whether the table holds no route for pin @p pin of device @p device on bus 0 of segment 0.
 */
static bool routing_test_unrouted(uint8_t device, uint8_t pin)
{
    RoutingTestRoute_t route;

    return !advanced_configuration_and_power_interface_interrupt_routing_find(&routing_test_table, 0u, 0u, device, pin,
                                                                              &route);
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
                                                                                         3u, 0u, &route) &&
                          !advanced_configuration_and_power_interface_interrupt_routing_find(&routing_test_table, 1u,
                                                                                             0u, 3u, 0u, &route),
                      "nor does device 3 INTA on bus 1, or on segment 1");
    kernel_test_measure(test, "routes", routing_test_table.count);
    routing_test_close(test);
}

/**
 * @brief A link gives its current interrupt, read once however many entries name it, with the trigger and polarity
 *        of the first interrupt descriptor of its _PRS; an entry whose SourceIndex is not 0 is unrouted.
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
    kernel_test_check(test, routing_test_unrouted(4u, 1u) && routing_test_table.links_unrouted == 1u,
                      "device 4 INTB, whose SourceIndex is 1, is unrouted and counted");
    kernel_test_check(test, routing_test_table.links == 4u && routing_test_table.count == 3u,
                      "four entries name a link, three are published");
    kernel_test_check(test, routing_test_integer("\\CRSN") == 1u, "LNKA's _CRS runs once for its three entries");
    routing_test_close(test);
}

/**
 * @brief A link routes nothing unless it is present, is a link, and its _CRS gives an interrupt its _PRS lists; a _CRS
 *        that gives no Buffer, or fails, is counted as a failure.
 */
KERNEL_TEST(a_link_routes_nothing_unless_its_current_interrupt_is_one_it_may_take)
{
    if (!routing_test_run(test, routing_unrouted_links_aml, sizeof(routing_unrouted_links_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.entries == 7u && routing_test_table.links == 7u,
                      "seven entries, each naming a device");
    kernel_test_check(test, routing_test_table.links_unrouted == 5u,
                      "an absent link, an IRQ its _PRS lacks, an empty IRQ, no _PRS and no link route nothing");
    kernel_test_check(test, routing_test_table.links_failed == 2u,
                      "a _CRS that returns an Integer and one that fails are failures");
    kernel_test_check(test,
                      routing_test_counts.statuses[ROUTING_TEST_STATUS(TYPE_MISMATCH)] >= 1u &&
                          routing_test_counts.statuses[ROUTING_TEST_STATUS(INDEX_OUT_OF_RANGE)] >= 1u,
                      "each failure is counted under its status");
    kernel_test_check(test, routing_test_table.count == 0u, "nothing is published");
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
 * @brief Only a root bridge _STA keeps in has its _PRT read, on the segment and bus its _SEG and _BBN give, a _CID
 *        naming it as a String among others; a device below it with a _PRT of its own is not read.
 */
KERNEL_TEST(only_the_root_bridges_present_by_their_status_are_read)
{
    if (!routing_test_run(test, routing_bridges_aml, sizeof(routing_bridges_aml), ROUTING_TEST_APIC))
        return;
    kernel_test_check(test, routing_test_table.bridges == 3u, "three root bridges, BRG0 none");
    kernel_test_check(test, routing_test_table.bridges_absent == 1u && routing_test_unrouted(1u, 0u),
                      "PCI0, absent, is counted and its _PRT not read");
    kernel_test_check(test, routing_test_table.bridges_unrouted == 1u, "PCI1, without a _PRT, is counted");
    kernel_test_check(test,
                      routing_test_table.count == 1u &&
                          routing_test_routes(1u, 2u, 5u, 0u, 40u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW, 0u),
                      "PCI2's one entry routes device 5 INTA on segment 1, bus 2, to GSI 40; BRG0's is not read");
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
                      routing_test_table.model == ROUTING_TEST_PIC &&
                          routing_test_routes(0u, 0u, 2u, 0u, 10u, ROUTING_TEST_LEVEL, ROUTING_TEST_LOW,
                                              routing_test_below_system_bus("LNKA")),
                      "device 2 INTA goes to LNKA's IRQ 10");
    routing_test_close(test);
}

/**
 * @brief A \_PIC (1) that fails is counted under its status, and nothing says the APIC model was taken.
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
 *        interrupt as four bytes, segment as two, bus, device, pin, trigger and polarity, little-endian; the link
 *        is left out, and changing any other field changes the fold.
 */
KERNEL_TEST(the_fold_covers_every_published_field)
{
    static RoutingTestTable_t table;
    const uint8_t expected[] = {
        0x01u, 0x02u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u,
        0x01u, 0x01u, 0x0Bu, 0x01u, 0x00u, 0x00u, 0x02u, 0x00u, 0x04u, 0x05u, 0x03u, 0x00u, 0x00u,
    };

    memset(&table, 0, sizeof(table));
    table.model = ROUTING_TEST_APIC;
    table.count = 2u;
    table.routes[0] = (RoutingTestRoute_t){.interrupt = 16u, .device = 3u, .level = 1u, .active_low = 1u, .link = 7u};
    table.routes[1] = (RoutingTestRoute_t){.interrupt = 0x10Bu, .segment = 2u, .bus = 4u, .device = 5u, .pin = 3u};

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
                      routing_test_descriptor(truncated, sizeof(truncated), &resource) &&
                          !advanced_configuration_and_power_interface_resources_interrupt(&resource, 0u, &interrupt),
                      "an Extended Interrupt whose table runs past its length names nothing");
    kernel_test_check(test,
                      routing_test_descriptor(register_descriptor, sizeof(register_descriptor), &resource) &&
                          !advanced_configuration_and_power_interface_resources_interrupt(&resource, 0u, &interrupt),
                      "a Generic Register names nothing");
}
