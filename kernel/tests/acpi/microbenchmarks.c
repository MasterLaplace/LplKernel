#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_microbenchmarks, KERNEL_TEST_STAGE_INITIALIZATION);

#define MICROBENCHMARK_TABLE_CAPACITY 64u   /**< Bytes of the buffer a made-up DSDT is written in. */
#define MICROBENCHMARK_TURNS          1000u /**< Turns of the loop, and lookups, of window W4. */
#define MICROBENCHMARK_SCOPE_CHILDREN 3346u /**< Children of the scope the lookups search (§7.4). */
#define MICROBENCHMARK_MAX_NODES      4096u /**< The node cap of the load that holds that scope. */
#define MICROBENCHMARK_NODE(type)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type

static uint8_t microbenchmark_table[MICROBENCHMARK_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t microbenchmark_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t microbenchmark_load;

/**
 * @brief The loop of window W4: LOOP sets Local0 to 0, then runs
 *        `While (LLess (Local0, 0x03E8)) { Increment (Local0) }` and returns Local0.
 */
static const uint8_t microbenchmark_loop_aml[] = {
    0x14u, 0x14u, 'L',   'O',   'O',   'P',   0x00u, 0x70u, 0x00u, 0x60u, 0xA2u,
    0x08u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u, 0x75u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief The NameSeg of child @p index of the wide scope: 'X' and three base-36 digits.
 */
static uint32_t microbenchmark_child_name(uint32_t index)
{
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const uint8_t name[4] = {'X', (uint8_t) digits[(index / 1296u) % 36u], (uint8_t) digits[(index / 36u) % 36u],
                             (uint8_t) digits[index % 36u]};

    return advanced_configuration_and_power_interface_machine_language_name_segment(name, 0u);
}

/**
 * @brief Loads the loop as a DSDT of @p revision, evaluates LOOP, and checks that it took its 1,000
 *        turns without allocating.
 */
static void microbenchmark_loop(KernelTest_t *test, uint8_t revision)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    uint32_t objects = 0u;

    if (!made_up_load_table(test, &microbenchmark_load, &microbenchmark_host, microbenchmark_table, revision,
                            microbenchmark_loop_aml, sizeof(microbenchmark_loop_aml)))
        return;
    objects = microbenchmark_load.heap.objects;
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(
                          &microbenchmark_load, "\\LOOP", NULL, 0u, &result) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
                          result.payload == MICROBENCHMARK_TURNS,
                      revision < 2u ? "the loop takes 1,000 turns under revision 1" : "the loop takes 1,000 turns");
    kernel_test_check(test, microbenchmark_load.heap.objects == objects, "the loop allocates nothing");
    kernel_test_measure(test, revision < 2u ? "back_edges_revision_1" : "back_edges",
                        microbenchmark_load.evaluation.iterations);
    advanced_configuration_and_power_interface_machine_language_close(&microbenchmark_load);
    advanced_configuration_and_power_interface_simulated_host_close(&microbenchmark_host);
}

/**
 * @brief Builds a scope of 3,346 children at the root, then looks 1,000 of them up by name, and checks
 *        that each lookup finds its child.
 */
static void microbenchmark_lookups(KernelTest_t *test)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &microbenchmark_load.space;
    uint32_t scope = 0u;
    uint32_t child = 0u;
    uint32_t found = 0u;
    bool built = true;

    advanced_configuration_and_power_interface_simulated_host_open(&microbenchmark_host);
    if (!kernel_test_check(
            test,
            advanced_configuration_and_power_interface_machine_language_open(
                &microbenchmark_load, &microbenchmark_host.host, MICROBENCHMARK_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
            "the namespace opens"))
        return;
    built = advanced_configuration_and_power_interface_machine_language_namespace_add_child(
                space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
                advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "WIDE", 0u),
                MICROBENCHMARK_NODE(DEVICE), 1u,
                &scope) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    for (uint32_t index = 0u; built && index < MICROBENCHMARK_SCOPE_CHILDREN; ++index)
        built = advanced_configuration_and_power_interface_machine_language_namespace_add_child(
                    space, scope, microbenchmark_child_name(index), MICROBENCHMARK_NODE(INTEGER), 1u, &child) ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    kernel_test_check(test, built, "the scope holds its 3,346 children");

    const uint32_t lookups = space->lookups;
    const uint32_t probes = space->probes;

    for (uint32_t turn = 0u; turn < MICROBENCHMARK_TURNS; ++turn)
    {
        const uint32_t name = microbenchmark_child_name((turn * 7u) % MICROBENCHMARK_SCOPE_CHILDREN);
        const uint32_t index =
            advanced_configuration_and_power_interface_machine_language_namespace_find_child(space, scope, name);

        found +=
            (index != 0u &&
             advanced_configuration_and_power_interface_machine_language_namespace_node(space, index)->name == name) ?
                1u :
                0u;
    }
    kernel_test_check(test, found == MICROBENCHMARK_TURNS && space->lookups - lookups == MICROBENCHMARK_TURNS,
                      "each of the 1,000 lookups finds its child");
    kernel_test_measure(test, "probes", space->probes - probes);
    advanced_configuration_and_power_interface_machine_language_close(&microbenchmark_load);
    advanced_configuration_and_power_interface_simulated_host_close(&microbenchmark_host);
}

/**
 * @brief Window W4 of §7.4, counted in ring 0 with `tools/instructions.sh --inside
 *        kernel_test_body_the_window_w4_loop_of_a_thousand_turns` against
 *        tools/acpi-microbenchmark.debug.budget: the loop of 1,000 turns under revision 2, then the same
 *        under revision 1, neither allocating.
 */
KERNEL_TEST(the_window_w4_loop_of_a_thousand_turns)
{
    microbenchmark_loop(test, 2u);
    microbenchmark_loop(test, 1u);
}

/**
 * @brief The third microbenchmark of window W4: 1,000 lookups in a scope of 3,346 children, counted
 *        with `tools/instructions.sh --inside kernel_test_body_a_thousand_lookups_in_a_wide_scope`; the
 *        scope it builds first costs more than the lookups, so it has no budget of its own.
 */
KERNEL_TEST(a_thousand_lookups_in_a_wide_scope) { microbenchmark_lookups(test); }
