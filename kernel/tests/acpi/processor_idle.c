#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/machine_language.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/resources.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_processor_idle, KERNEL_TEST_STAGE_INITIALIZATION);

#define IDLE_FIXED_HARDWARE  0x7Fu       /**< The FFixedHW address space. */
#define IDLE_SYSTEM_IO       0x01u       /**< The SystemIO address space. */
#define IDLE_NVS             0x7FF00000u /**< Where the made-up NVS region lies in simulated memory. */
#define IDLE_COMMAND_PORT    0xB2u       /**< The made-up FADT's SMI_CMD. */
#define IDLE_CONTROL_COMMAND 0x85u       /**< Its CST_CNT. */
#define IDLE_STATUS(name)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define IDLE_CONTROL(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_##name
#define IDLE_MODE(outcome)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_##outcome
#define IDLE_VALUE(kind)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind

typedef AdvancedConfigurationAndPowerInterfaceIdleState_t IdleState_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t IdleValue_t;

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t idle_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t idle_load;
static AdvancedConfigurationAndPowerInterfaceInitialization_t idle_counts;
static AdvancedConfigurationAndPowerInterfaceMadtState_t idle_madt;
static AdvancedConfigurationAndPowerInterfaceFixedDescription_t idle_fadt;
static AdvancedConfigurationAndPowerInterfaceInitializationMachine_t idle_machine;
static AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t idle_table;
static uint8_t idle_bytes[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static MadeUpAml_t idle_aml;
static MadeUpAml_t idle_scope;
static MadeUpAml_t idle_method;
static MadeUpAml_t idle_branch;
static MadeUpAml_t idle_package;
static MadeUpAml_t idle_state_body;
static MadeUpAml_t idle_buffer;

/**
 * @brief A state of FFixedHW with Intel's vendor and the native MWAIT class (ACPI 6.6 §8.4.1.1, Intel's
 *        processor vendor-specific ACPI document), hint @p hint.
 */
static IdleState_t idle_monitor_wait(uint64_t hint, uint32_t type, uint32_t latency, uint32_t power)
{
    const IdleState_t state = {
        .entry = {.address = hint, .space = IDLE_FIXED_HARDWARE, .bit_width = 1u, .bit_offset = 2u, .access_size = 1u},
        .type = type,
        .latency = latency,
        .power = power,
    };

    return state;
}

/**
 * @brief A state entered by reading the I/O port @p port, 8 bits wide.
 */
static IdleState_t idle_port(uint64_t port, uint8_t access, uint32_t type, uint32_t latency, uint32_t power)
{
    const IdleState_t state = {
        .entry = {.address = port, .space = IDLE_SYSTEM_IO, .bit_width = 8u, .access_size = access},
        .type = type,
        .latency = latency,
        .power = power,
    };

    return state;
}

/**
 * @brief Opens the simulated host and empties the AML; the machine has CPU slots of the @p count UIDs at
 *        @p identifiers, a FADT whose SMI_CMD and CST_CNT are set, and a platform step 0 switched.
 */
static void idle_begin(const uint32_t *identifiers, uint32_t count)
{
    memset(&idle_madt, 0, sizeof(idle_madt));
    memset(&idle_fadt, 0, sizeof(idle_fadt));
    idle_madt.processor_count = count;
    for (uint32_t slot = 0u; slot < count; ++slot)
        idle_madt.processors[slot].processor_unique_identifier = identifiers[slot];
    idle_fadt.present = true;
    idle_fadt.system_management_interrupt_command = IDLE_COMMAND_PORT;
    idle_fadt.idle_state_command = IDLE_CONTROL_COMMAND;
    idle_machine.madt = &idle_madt;
    idle_machine.monitor_wait = true;
    idle_machine.fadt = &idle_fadt;
    idle_machine.mode = IDLE_MODE(SWITCHED);
    advanced_configuration_and_power_interface_simulated_host_open(&idle_host);
    made_up_aml_begin(&idle_aml);
}

/**
 * @brief Loads the AML written as a DSDT of revision 2, then initialises its namespace.
 */
static bool idle_run(KernelTest_t *test)
{
    if (!kernel_test_check(test, !idle_aml.overflowed, "the made-up AML fits") ||
        !made_up_load_on(test, &idle_load, &idle_host.host, idle_bytes, 2u, idle_aml.bytes, idle_aml.length))
        return false;
    advanced_configuration_and_power_interface_initialize_namespace(&idle_load, &idle_machine, &idle_counts,
                                                                    &idle_table);
    return true;
}

/**
 * @brief Checks that the evaluations left nothing behind, then closes the load.
 */
static void idle_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &idle_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && idle_load.temporary_count == 0u,
                      "every evaluation leaves no frame, value, operator or temporary node behind");
    kernel_test_check(test, idle_load.heap.stale_handles == 0u, "no retain or release met a dead object");
    kernel_test_check(test, idle_load.services.notification_count == 0u, "no Notify is left in the ring");
    advanced_configuration_and_power_interface_machine_language_close(&idle_load);
    advanced_configuration_and_power_interface_simulated_host_close(&idle_host);
}

/**
 * @brief Appends ResourceTemplate () { Register (...) } for @p entry, as the Buffer of 17 bytes iasl
 *        writes: the Generic Register descriptor, then the End Tag.
 */
static void idle_register(MadeUpAml_t *aml, const AdvancedConfigurationAndPowerInterfaceGenericAddress_t *entry)
{
    static const uint8_t lead = 0x11u;
    uint8_t descriptor[17] = {
        0x82u, 0x0Cu, 0x00u, entry->space, entry->bit_width, entry->bit_offset, entry->access_size};

    for (uint32_t byte = 0u; byte < 8u; ++byte)
        descriptor[7u + byte] = (uint8_t) (entry->address >> (8u * byte));
    descriptor[15] = 0x79u;
    made_up_aml_begin(&idle_buffer);
    made_up_aml_integer(&idle_buffer, sizeof(descriptor));
    made_up_aml_raw(&idle_buffer, descriptor, sizeof(descriptor));
    made_up_aml_construct(aml, &lead, 1u, &idle_buffer);
}

/**
 * @brief Appends Package (4) { the Register of @p state, its Type, Latency and Power }.
 */
static void idle_state(MadeUpAml_t *aml, const IdleState_t *state)
{
    static const uint8_t lead[] = {0x12u};
    static const uint8_t elements = 4u;

    made_up_aml_begin(&idle_state_body);
    made_up_aml_raw(&idle_state_body, &elements, 1u);
    idle_register(&idle_state_body, &state->entry);
    made_up_aml_integer(&idle_state_body, state->type);
    made_up_aml_integer(&idle_state_body, state->latency);
    made_up_aml_integer(&idle_state_body, state->power);
    made_up_aml_construct(aml, lead, sizeof(lead), &idle_state_body);
}

/**
 * @brief Appends Package () { @p count, then the @p state_count @p states }.
 */
static void idle_states(MadeUpAml_t *aml, uint64_t count, const IdleState_t *states, uint32_t state_count)
{
    static const uint8_t lead[] = {0x12u};
    const uint8_t elements = (uint8_t) (state_count + 1u);

    made_up_aml_begin(&idle_package);
    made_up_aml_raw(&idle_package, &elements, 1u);
    made_up_aml_integer(&idle_package, count);
    for (uint32_t state = 0u; state < state_count; ++state)
        idle_state(&idle_package, &states[state]);
    made_up_aml_construct(aml, lead, sizeof(lead), &idle_package);
}

/**
 * @brief Appends Name (@p name, ...) and the term at @p aml's end comes after it: Name's opcode and NameSeg.
 */
static void idle_name(MadeUpAml_t *aml, const char *name)
{
    static const uint8_t name_byte = 0x08u;

    made_up_aml_raw(aml, &name_byte, 1u);
    made_up_aml_name(aml, name);
}

/**
 * @brief Appends Name (_CST, Package () { @p count, then the @p state_count @p states }).
 */
static void idle_static(MadeUpAml_t *aml, uint64_t count, const IdleState_t *states, uint32_t state_count)
{
    idle_name(aml, "_CST");
    idle_states(aml, count, states, state_count);
}

/**
 * @brief Appends Processor (@p name, @p identifier, 0x410, 6) { @p body } to the DSDT.
 */
static void idle_processor(const char *name, uint8_t identifier, const MadeUpAml_t *body)
{
    made_up_aml_processor(&idle_aml, name, identifier, 0x410u, 6u, body);
}

/**
 * @brief Appends Device (@p name) { Name (_HID, @p hardware), Name (_UID, @p unique), @p body } to @p aml.
 */
static void idle_device(MadeUpAml_t *aml, const char *name, const char *hardware, uint64_t unique,
                        const MadeUpAml_t *body)
{
    static const uint8_t string = 0x0Du;
    static const uint8_t end = 0x00u;
    MadeUpAml_t device;

    made_up_aml_begin(&device);
    idle_name(&device, "_HID");
    made_up_aml_raw(&device, &string, 1u);
    made_up_aml_raw(&device, (const uint8_t *) hardware, 8u);
    made_up_aml_raw(&device, &end, 1u);
    idle_name(&device, "_UID");
    made_up_aml_integer(&device, unique);
    made_up_aml_raw(&device, body->bytes, body->length);
    made_up_aml_device(aml, name, &device);
}

/**
 * @brief Whether slot @p slot published the @p count states at @p expected, field by field.
 */
static bool idle_published(uint32_t slot, const IdleState_t *expected, uint32_t count)
{
    const AdvancedConfigurationAndPowerInterfaceProcessorIdle_t *idle = &idle_table.slots[slot];

    if (idle->processor == 0u || idle->count != count)
        return false;
    for (uint32_t state = 0u; state < count; ++state)
    {
        const IdleState_t *published = &idle->states[state];

        if (published->entry.space != expected[state].entry.space ||
            published->entry.bit_width != expected[state].entry.bit_width ||
            published->entry.bit_offset != expected[state].entry.bit_offset ||
            published->entry.access_size != expected[state].entry.access_size ||
            published->entry.address != expected[state].entry.address || published->type != expected[state].type ||
            published->latency != expected[state].latency || published->power != expected[state].power)
            return false;
    }
    return true;
}

/**
 * @brief Appends Intel's pattern to the DSDT: an NVS region at the root, Field { PDCB, 32, C6LT, 16, C7LT, 16 },
 *        then Processor (CPU0, @p identifier) holding Name (C1TM) a C1 of FFixedHW halt, Name (C6TM) and
 *        Name (C7TM) a C2 and a C3 of ports 0x415 and 0x416, Name (MWC1) the Register of MWAIT C1, and a
 *        Serialized _CST that runs
 *        `If (And (PDCB, 0x200)) { Store (MWC1, Index (C1TM, Zero)) }`,
 *        `Store (C6LT, Index (C6TM, 2))`, `Store (C7LT, Index (C7TM, 2))`, then
 *        `Return (Package () { 3, C1TM, C6TM, C7TM })`: names, which are read where they point.
 */
static void idle_intel_pattern(uint8_t identifier)
{
    static const MadeUpFieldUnit_t units[] = {
        {"PDCB", 32u},
        {"C6LT", 16u},
        {"C7LT", 16u}
    };
    static const uint8_t if_lead[] = {0xA0u};
    static const uint8_t package_lead[] = {0x12u};
    static const uint8_t predicate[] = {0x7Bu, 'P', 'D', 'C', 'B', 0x0Bu, 0x00u, 0x02u, 0x00u};
    static const uint8_t patch_register[] = {0x70u, 'M', 'W', 'C', '1', 0x88u, 'C', '1', 'T', 'M', 0x00u, 0x00u};
    static const uint8_t patch_latencies[] = {0x70u, 'C',   '6',   'L',   'T',   0x88u, 'C',   '6',  'T',
                                              'M',   0x0Au, 0x02u, 0x00u, 0x70u, 'C',   '7',   'L',  'T',
                                              0x88u, 'C',   '7',   'T',   'M',   0x0Au, 0x02u, 0x00u};
    static const uint8_t return_byte = 0xA4u;
    static const uint8_t names[] = {0x04u, 0x0Au, 0x03u, 'C', '1', 'T', 'M', 'C', '6', 'T', 'M', 'C', '7', 'T', 'M'};
    const IdleState_t halt = {.entry = {.space = IDLE_FIXED_HARDWARE}, .type = 1u, .latency = 1u, .power = 1000u};
    const IdleState_t deeper = idle_port(0x415u, 0u, 2u, 0u, 350u);
    const IdleState_t deepest = idle_port(0x416u, 0u, 3u, 0u, 200u);
    const IdleState_t monitor_wait = idle_monitor_wait(0x00u, 1u, 1u, 1000u);

    made_up_aml_region(&idle_aml, "GNVS", 0x00u, IDLE_NVS, 8u);
    made_up_aml_field(&idle_aml, "GNVS", 0x00u, units, 3u);
    made_up_aml_begin(&idle_scope);
    idle_name(&idle_scope, "C1TM");
    idle_state(&idle_scope, &halt);
    idle_name(&idle_scope, "C6TM");
    idle_state(&idle_scope, &deeper);
    idle_name(&idle_scope, "C7TM");
    idle_state(&idle_scope, &deepest);
    idle_name(&idle_scope, "MWC1");
    idle_register(&idle_scope, &monitor_wait.entry);
    made_up_aml_begin(&idle_branch);
    made_up_aml_raw(&idle_branch, predicate, sizeof(predicate));
    made_up_aml_raw(&idle_branch, patch_register, sizeof(patch_register));
    made_up_aml_begin(&idle_method);
    made_up_aml_construct(&idle_method, if_lead, sizeof(if_lead), &idle_branch);
    made_up_aml_raw(&idle_method, patch_latencies, sizeof(patch_latencies));
    made_up_aml_raw(&idle_method, &return_byte, 1u);
    made_up_aml_begin(&idle_package);
    made_up_aml_raw(&idle_package, names, sizeof(names));
    made_up_aml_construct(&idle_method, package_lead, sizeof(package_lead), &idle_package);
    made_up_aml_method(&idle_scope, "_CST", 0x08u, &idle_method);
    idle_processor("CPU0", identifier, &idle_scope);
}

/**
 * @brief Writes the NVS fields of Intel's pattern: PDCB, C6LT and C7LT.
 */
static void idle_intel_values(uint32_t capabilities, uint16_t deeper, uint16_t deepest)
{
    const uint8_t values[8] = {(uint8_t) capabilities,
                               (uint8_t) (capabilities >> 8u),
                               (uint8_t) (capabilities >> 16u),
                               (uint8_t) (capabilities >> 24u),
                               (uint8_t) deeper,
                               (uint8_t) (deeper >> 8u),
                               (uint8_t) deepest,
                               (uint8_t) (deepest >> 8u)};

    (void) advanced_configuration_and_power_interface_simulated_host_poke(&idle_host, IDLE_NVS, values, sizeof(values));
}

/**
 * @brief The AMD pattern: Device (C001) of _HID "ACPI0007" and _UID @p unique, whose _CST is a static
 *        package: a C1 of FFixedHW, then a C2 and a C3 of ports 0x414 and 0x415.
 */
static const IdleState_t *idle_amd_pattern(uint64_t unique)
{
    static IdleState_t states[3];

    states[0] = (IdleState_t){
        .entry = {.space = IDLE_FIXED_HARDWARE, .bit_width = 2u, .bit_offset = 2u},
        .type = 1u,
        .latency = 1u
    };
    states[1] = idle_port(0x414u, 1u, 2u, 18u, 0u);
    states[2] = idle_port(0x415u, 1u, 3u, 350u, 0u);
    made_up_aml_begin(&idle_scope);
    idle_static(&idle_scope, 3u, states, 3u);
    idle_device(&idle_aml, "C001", "ACPI0007", unique, &idle_scope);
    return states;
}

/**
 * @brief Window W3 of §7.4: Intel's pattern, a Serialized _CST that patches a package of buffers from
 *        NVS fields and returns names, and AMD's static package, each bound to its CPU slot by its UID, not
 *        by its place: every state is published as firmware lists it, type 3 included.
 */
KERNEL_TEST(the_window_w3_c_states_of_an_intel_and_an_amd_processor)
{
    static const uint32_t identifiers[] = {2u, 1u};
    const IdleState_t intel[3] = {idle_monitor_wait(0x00u, 1u, 1u, 1000u), idle_port(0x415u, 0u, 2u, 0x68u, 350u),
                                  idle_port(0x416u, 0u, 3u, 0x9Au, 200u)};

    idle_begin(identifiers, 2u);
    idle_intel_pattern(1u);

    const IdleState_t *amd = idle_amd_pattern(2u);

    idle_intel_values(0x318u, 0x68u, 0x9Au);
    if (!idle_run(test))
        return;
    kernel_test_check(test, idle_table.slot_count == 2u, "one table entry per CPU slot");
    kernel_test_check(test, idle_published(0u, amd, 3u), "slot 0, of UID 2, publishes AMD's three states");
    kernel_test_check(test, idle_published(1u, intel, 3u),
                      "slot 1, of UID 1, publishes Intel's states as its _CST patched them: MWAIT C1, then the "
                      "latencies NVS holds");
    kernel_test_check(test,
                      idle_counts.processor_idle == 2u && idle_counts.processor_idle_failed == 0u &&
                          idle_counts.processor_idle_unbound == 0u && idle_counts.idle_states_dropped == 0u,
                      "two evaluations, no failure, nothing unbound or dropped");
    kernel_test_measure_hexadecimal(test, "idle_fold",
                                    advanced_configuration_and_power_interface_processor_idle_fold(&idle_table));
    idle_close(test);
}

/**
 * @brief Intel's _CST without bit 9 in PDCB keeps its C1 of FFixedHW halt: the package it returns is the
 *        one its branches built.
 */
KERNEL_TEST(intel_s_pattern_without_monitor_wait_keeps_its_halt)
{
    static const uint32_t identifiers[] = {1u};
    const IdleState_t intel[3] = {
        {.entry = {.space = IDLE_FIXED_HARDWARE}, .type = 1u, .latency = 1u, .power = 1000u},
        idle_port(0x415u, 0u, 2u, 0x50u, 350u),
        idle_port(0x416u, 0u, 3u, 0x60u, 200u)
    };

    idle_begin(identifiers, 1u);
    idle_intel_pattern(1u);
    idle_intel_values(0x018u, 0x50u, 0x60u);
    if (!idle_run(test))
        return;
    kernel_test_check(test, idle_published(0u, intel, 3u), "the halt C1, then the NVS latencies");
    idle_close(test);
}

/**
 * @brief A processor object binds to the first CPU slot of its UID; one whose UID no slot has, one whose
 *        slot an earlier object took, a device that is no processor and a processor without _CST publish
 *        nothing; a Processor inside a Device is found.
 */
KERNEL_TEST(a_processor_binds_to_the_cpu_slot_of_its_uid_or_publishes_nothing)
{
    static const uint32_t identifiers[] = {5u, 6u, 7u};
    const IdleState_t only = idle_monitor_wait(0x00u, 1u, 1u, 1000u);
    MadeUpAml_t socket;

    idle_begin(identifiers, 3u);
    made_up_aml_begin(&idle_scope);
    idle_static(&idle_scope, 1u, &only, 1u);
    idle_processor("CPU0", 5u, &idle_scope);
    idle_processor("CPU1", 5u, &idle_scope);
    idle_processor("CPU2", 9u, &idle_scope);
    idle_device(&idle_aml, "BRDG", "ACPI0008", 6u, &idle_scope);
    made_up_aml_begin(&idle_scope);
    idle_device(&idle_aml, "C007", "ACPI0007", 7u, &idle_scope);
    made_up_aml_begin(&idle_scope);
    idle_static(&idle_scope, 1u, &only, 1u);
    made_up_aml_begin(&socket);
    made_up_aml_processor(&socket, "CPU6", 6u, 0x410u, 6u, &idle_scope);
    made_up_aml_device(&idle_aml, "SCK0", &socket);
    if (!idle_run(test))
        return;
    kernel_test_check(test, idle_published(0u, &only, 1u) && idle_published(1u, &only, 1u),
                      "CPU0 publishes in slot 0, CPU6 in slot 1, inside SCK0");
    kernel_test_check(test, idle_table.slots[2].processor == 0u && idle_table.slots[2].count == 0u,
                      "slot 2's processor object has no _CST, and publishes nothing");
    kernel_test_check(test, idle_counts.processor_idle == 2u && idle_counts.processor_idle_unbound == 2u,
                      "two evaluations; CPU1, whose slot CPU0 took, and CPU2, of no slot, are unbound");
    idle_close(test);
}

/**
 * @brief Appends Processor (@p name, @p identifier) { Method (_CST) { Return (@p bytes) } }.
 */
static void idle_returning(const char *name, uint8_t identifier, const uint8_t *bytes, uint32_t length)
{
    static const uint8_t return_byte = 0xA4u;

    made_up_aml_begin(&idle_method);
    made_up_aml_raw(&idle_method, &return_byte, 1u);
    made_up_aml_raw(&idle_method, bytes, length);
    made_up_aml_begin(&idle_scope);
    made_up_aml_method(&idle_scope, "_CST", 0x00u, &idle_method);
    idle_processor(name, identifier, &idle_scope);
}

/**
 * @brief Appends a Buffer of the @p length bytes at @p bytes.
 */
static void idle_buffer_of(MadeUpAml_t *aml, const uint8_t *bytes, uint32_t length)
{
    static const uint8_t lead = 0x11u;

    made_up_aml_begin(&idle_buffer);
    made_up_aml_integer(&idle_buffer, length);
    made_up_aml_raw(&idle_buffer, bytes, length);
    made_up_aml_construct(aml, &lead, 1u, &idle_buffer);
}

/**
 * @brief Starts in idle_package a _CST package of @p elements elements whose Count is @p count.
 */
static void idle_package_begin(uint8_t elements, uint64_t count)
{
    made_up_aml_begin(&idle_package);
    made_up_aml_raw(&idle_package, &elements, 1u);
    made_up_aml_integer(&idle_package, count);
}

/**
 * @brief Appends to idle_package the state idle_state_body holds, its element count first.
 */
static void idle_package_add(void)
{
    static const uint8_t lead[] = {0x12u};

    made_up_aml_construct(&idle_package, lead, sizeof(lead), &idle_state_body);
}

/**
 * @brief Appends Processor (@p name, @p identifier) { Name (_CST, the package idle_package holds) }.
 */
static void idle_package_end(const char *name, uint8_t identifier)
{
    static const uint8_t lead[] = {0x12u};

    made_up_aml_begin(&idle_scope);
    idle_name(&idle_scope, "_CST");
    made_up_aml_construct(&idle_scope, lead, sizeof(lead), &idle_package);
    idle_processor(name, identifier, &idle_scope);
}

/**
 * @brief Starts in idle_state_body a state of @p elements elements whose Register is MWAIT C1, or the
 *        @p length bytes at @p bytes as its Buffer when they are given.
 */
static void idle_state_start(uint8_t elements, const uint8_t *bytes, uint32_t length)
{
    const IdleState_t monitor_wait = idle_monitor_wait(0x00u, 1u, 1u, 1000u);

    made_up_aml_begin(&idle_state_body);
    made_up_aml_raw(&idle_state_body, &elements, 1u);
    if (bytes != NULL)
        idle_buffer_of(&idle_state_body, bytes, length);
    else
        idle_register(&idle_state_body, &monitor_wait.entry);
}

/**
 * @brief Appends the Type, Latency and Power of a state to idle_state_body, then the state to idle_package.
 */
static void idle_state_end(uint64_t type, uint64_t latency, uint64_t power)
{
    made_up_aml_integer(&idle_state_body, type);
    made_up_aml_integer(&idle_state_body, latency);
    made_up_aml_integer(&idle_state_body, power);
    idle_package_add();
}

/**
 * @brief A _CST that fails, or returns no Package, an empty one, one whose Count is no Integer, below 1, or
 *        other than its sub-packages, fails as a whole, as Linux refuses it: nothing is published and the
 *        failure is counted with its status.
 */
KERNEL_TEST(a_malformed_cst_package_publishes_nothing_and_is_counted)
{
    static const uint32_t identifiers[] = {1u, 2u, 3u, 4u, 5u, 6u};
    static const uint8_t divide[] = {0x78u, 0x01u, 0x00u, 0x00u, 0x00u};
    static const uint8_t integer[] = {0x0Au, 0x03u};
    static const uint8_t miscounted[] = {0x12u, 0x04u, 0x01u, 0x0Au, 0x02u};
    static const uint8_t empty_package[] = {0x12u, 0x02u, 0x00u};
    static const uint8_t zero_count[] = {0x12u, 0x03u, 0x01u, 0x00u};
    static const uint8_t string_count[] = {0x12u, 0x05u, 0x01u, 0x0Du, '1', 0x00u};

    idle_begin(identifiers, 6u);
    made_up_aml_begin(&idle_method);
    made_up_aml_raw(&idle_method, divide, sizeof(divide));
    made_up_aml_begin(&idle_scope);
    made_up_aml_method(&idle_scope, "_CST", 0x00u, &idle_method);
    idle_processor("CPU1", 1u, &idle_scope);
    idle_returning("CPU2", 2u, integer, sizeof(integer));
    idle_returning("CPU3", 3u, miscounted, sizeof(miscounted));
    idle_returning("CPU4", 4u, empty_package, sizeof(empty_package));
    idle_returning("CPU5", 5u, zero_count, sizeof(zero_count));
    idle_returning("CPU6", 6u, string_count, sizeof(string_count));
    if (!idle_run(test))
        return;

    uint32_t empty = 0u;

    for (uint32_t slot = 0u; slot < 6u; ++slot)
        empty += (idle_table.slots[slot].processor != 0u && idle_table.slots[slot].count == 0u &&
                  idle_table.slots[slot].skipped == 0u) ?
                     1u :
                     0u;
    kernel_test_check(test, empty == 6u, "each _CST was evaluated, and publishes and skips nothing");
    kernel_test_check(test,
                      idle_counts.processor_idle == 6u && idle_counts.processor_idle_failed == 6u &&
                          idle_counts.statuses[IDLE_STATUS(DIVIDE_BY_ZERO)] == 1u &&
                          idle_counts.statuses[IDLE_STATUS(TYPE_MISMATCH)] == 5u,
                      "six failures: one divide_by_zero, five type_mismatch");
    idle_close(test);
}

/**
 * @brief A faulty state is left out and counted on its CPU slot, and the states around it are published,
 *        as Linux skips it: a state of three elements, a Register with only an End Tag, a cut Generic
 *        Register, or a Generic Register shorter than 12 bytes, a Type that is a String, a Latency past
 *        32 bits. The Generic Register is found after another descriptor, a Latency of 32 bits is kept, and
 *        a register of any space, SystemMemory here, is published.
 */
KERNEL_TEST(a_faulty_state_is_left_out_and_counted_on_its_slot)
{
    static const uint32_t identifiers[] = {1u};
    static const uint8_t end_only[] = {0x79u, 0x00u};
    static const uint8_t cut[] = {0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x01u};
    static const uint8_t short_register[] = {0x82u, 0x02u, 0x00u, 0x7Fu, 0x01u, 0x79u, 0x00u};
    static const uint8_t after_interrupt[] = {0x22u, 0x01u, 0x00u, 0x82u, 0x0Cu, 0x00u, 0x01u, 0x08u, 0x00u, 0x01u,
                                              0x15u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x79u, 0x00u};
    static const uint8_t string_one[] = {0x0Du, '1', 0x00u};
    static const uint8_t elements = 4u;
    const IdleState_t memory = {
        .entry = {.address = 0xFED00010u, .space = 0x00u, .bit_width = 32u, .access_size = 3u},
        .type = 3u,
        .latency = 200u,
        .power = 100u
    };
    const IdleState_t expected[3] = {idle_monitor_wait(0x00u, 1u, 1u, 1000u),
                                     idle_port(0x415u, 1u, 2u, 0xFFFFFFFFu, 350u), memory};

    idle_begin(identifiers, 1u);
    idle_package_begin(10u, 9u);
    idle_state_start(4u, NULL, 0u);
    idle_state_end(1u, 1u, 1000u);
    idle_state_start(3u, NULL, 0u);
    made_up_aml_integer(&idle_state_body, 2u);
    made_up_aml_integer(&idle_state_body, 2u);
    idle_package_add();
    idle_state_start(4u, end_only, sizeof(end_only));
    idle_state_end(2u, 2u, 2u);
    idle_state_start(4u, cut, sizeof(cut));
    idle_state_end(2u, 2u, 2u);
    idle_state_start(4u, short_register, sizeof(short_register));
    idle_state_end(2u, 2u, 2u);
    idle_state_start(4u, NULL, 0u);
    made_up_aml_raw(&idle_state_body, string_one, sizeof(string_one));
    made_up_aml_integer(&idle_state_body, 2u);
    made_up_aml_integer(&idle_state_body, 2u);
    idle_package_add();
    idle_state_start(4u, NULL, 0u);
    idle_state_end(2u, 0x100000000ull, 2u);
    idle_state_start(4u, after_interrupt, sizeof(after_interrupt));
    idle_state_end(2u, 0xFFFFFFFFu, 350u);
    made_up_aml_begin(&idle_state_body);
    made_up_aml_raw(&idle_state_body, &elements, 1u);
    idle_register(&idle_state_body, &memory.entry);
    idle_state_end(memory.type, memory.latency, memory.power);
    idle_package_end("CPU0", 1u);
    if (!idle_run(test))
        return;
    kernel_test_check(test, idle_published(0u, expected, 3u),
                      "MWAIT C1, the port read after an IRQ descriptor, and the SystemMemory register are published");
    kernel_test_check(test, idle_table.slots[0].skipped == 6u, "six faulty states are counted on the slot");
    kernel_test_check(test, idle_counts.processor_idle == 1u && idle_counts.processor_idle_failed == 0u,
                      "the _CST does not fail");
    idle_close(test);
}

/**
 * @brief A _CST of nine states, the fourth faulty, publishes seven, as Linux; the ninth, past them, is
 *        counted and not read, though it is faulty too (§6.1).
 */
KERNEL_TEST(seven_states_are_published_and_the_ones_after_them_are_not_read)
{
    static const uint32_t identifiers[] = {1u};
    IdleState_t expected[7];
    uint32_t published = 0u;

    idle_begin(identifiers, 1u);
    idle_package_begin(10u, 9u);
    for (uint32_t state = 0u; state < 9u; ++state)
    {
        if (state == 3u || state == 8u)
        {
            idle_state_start(3u, NULL, 0u);
            made_up_aml_integer(&idle_state_body, 3u);
            made_up_aml_integer(&idle_state_body, 3u);
            idle_package_add();
            continue;
        }
        expected[published] = idle_monitor_wait(0x10u * state, (state < 2u) ? state + 1u : 3u, 10u * state, 100u);
        idle_state(&idle_package, &expected[published]);
        ++published;
    }
    idle_package_end("CPU0", 1u);
    if (!idle_run(test))
        return;
    kernel_test_check(
        test, idle_published(0u, expected, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_MAX_STATES),
        "the seven well-formed states before the cap are published");
    kernel_test_check(test, idle_table.slots[0].skipped == 1u && idle_counts.idle_states_dropped == 1u,
                      "the fourth is skipped; the ninth is dropped unread, not skipped");
    kernel_test_check(test, idle_counts.processor_idle_failed == 0u, "the _CST does not fail");
    idle_close(test);
}

/**
 * @brief Appends Processor (@p name, @p identifier) { Name (_STA, @p status), Name (_CST, one MWAIT C1) }
 *        to @p aml, or no _STA when @p status is above 0xFF.
 */
static void idle_present(MadeUpAml_t *aml, const char *name, uint8_t identifier, uint64_t status)
{
    const IdleState_t only = idle_monitor_wait(0x00u, 1u, 1u, 1000u);

    made_up_aml_begin(&idle_scope);
    if (status <= 0xFFu)
        made_up_aml_name_integer(&idle_scope, "_STA", status);
    idle_static(&idle_scope, 1u, &only, 1u);
    made_up_aml_processor(aml, name, identifier, 0x410u, 6u, &idle_scope);
}

/**
 * @brief Step 8 evaluates the _CST of the processor objects Linux attaches (v7.3 acpi_bus_attach,
 *        acpi_processor_add): present or functioning and enabled, with no device above them neither present
 *        nor functioning; a _STA that fails reads 0, and a missing one 0x0F. A processor kept out takes no
 *        slot, and each device's _STA is read once.
 */
KERNEL_TEST(only_the_processors_linux_attaches_publish_their_c_states)
{
    static const uint32_t identifiers[] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u};
    static const uint8_t counted_absent[] = {0x75u, '\\', 'D', '0', 'R', 'D', 0xA4u, 0x00u};
    static const uint8_t failing[] = {0xA4u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u};
    const IdleState_t only = idle_monitor_wait(0x00u, 1u, 1u, 1000u);
    MadeUpAml_t device;
    MadeUpAml_t body;

    idle_begin(identifiers, 9u);
    made_up_aml_name_integer(&idle_aml, "D0RD", 0u);
    idle_present(&idle_aml, "CPU0", 1u, 0x100u);
    idle_present(&idle_aml, "CPU1", 2u, 0x0Du);
    idle_present(&idle_aml, "CPU2", 3u, 0x08u);
    idle_present(&idle_aml, "CPU3", 4u, 0x0Au);
    made_up_aml_begin(&device);
    made_up_aml_begin(&body);
    made_up_aml_raw(&body, counted_absent, sizeof(counted_absent));
    made_up_aml_method(&device, "_STA", 0x00u, &body);
    idle_present(&device, "CPU4", 5u, 0x100u);
    idle_present(&device, "CPU9", 10u, 0x100u);
    made_up_aml_device(&idle_aml, "DEV0", &device);
    made_up_aml_begin(&device);
    made_up_aml_name_integer(&device, "_STA", 0x08u);
    idle_present(&device, "CPU5", 6u, 0x100u);
    made_up_aml_device(&idle_aml, "DEV1", &device);
    made_up_aml_begin(&idle_scope);
    made_up_aml_begin(&body);
    made_up_aml_raw(&body, failing, sizeof(failing));
    made_up_aml_method(&idle_scope, "_STA", 0x00u, &body);
    idle_static(&idle_scope, 1u, &only, 1u);
    idle_processor("CPU6", 7u, &idle_scope);
    idle_present(&idle_aml, "CPU7", 8u, 0x00u);
    idle_present(&idle_aml, "CPU8", 8u, 0x100u);
    if (!idle_run(test))
        return;
    kernel_test_check(test,
                      idle_published(0u, &only, 1u) && idle_published(3u, &only, 1u) && idle_published(5u, &only, 1u) &&
                          idle_published(7u, &only, 1u),
                      "CPU0 without _STA, CPU3 functioning and enabled, CPU5 below a functioning device, and CPU8 "
                      "publish");
    kernel_test_check(test,
                      idle_table.slots[1].processor == 0u && idle_table.slots[2].processor == 0u &&
                          idle_table.slots[4].processor == 0u && idle_table.slots[6].processor == 0u &&
                          idle_table.slots[8].processor == 0u,
                      "CPU1 not enabled, CPU2 neither present nor enabled, CPU4 and CPU9 below an absent device, and "
                      "CPU6 whose _STA fails publish nothing");
    kernel_test_check(test, idle_counts.processor_idle_absent == 6u && idle_counts.processor_idle_unbound == 0u,
                      "six processors kept out, CPU7 among them, which leaves its slot to CPU8");
    kernel_test_check(test, made_up_root_node(&idle_load, "D0RD")->value.payload == 1u,
                      "DEV0's _STA is read once for its two processors");
    kernel_test_check(test, idle_counts.statuses[IDLE_STATUS(DIVIDE_BY_ZERO)] == 1u, "the _STA that fails is counted");
    idle_close(test);
}

/**
 * @brief How many times the simulated host was asked to write @p value to @p port.
 */
static uint32_t idle_port_writes(uint16_t port, uint32_t value)
{
    uint32_t writes = 0u;

    for (uint32_t access = 0u; access < idle_host.access_count &&
                               access < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;
         ++access)
        writes += (idle_host.accesses[access].kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_WRITE &&
                   idle_host.accesses[access].port == port && idle_host.accesses[access].value == value &&
                   idle_host.accesses[access].width == 1u) ?
                      1u :
                      0u;
    return writes;
}

/**
 * @brief The outcome of step 8's write of CST_CNT for one machine: the mode step 0 ended in, SMI_CMD and
 *        CST_CNT; and how many writes of CST_CNT to SMI_CMD it made.
 */
static uint32_t idle_control(KernelTest_t *test, uint8_t mode, uint32_t command_port, uint8_t command, bool fadt,
                             uint32_t *out_writes)
{
    static const uint32_t identifiers[] = {1u};
    uint32_t control = 0xFFu;

    idle_begin(identifiers, 1u);
    idle_machine.mode = mode;
    idle_machine.fadt = fadt ? &idle_fadt : NULL;
    idle_fadt.system_management_interrupt_command = command_port;
    idle_fadt.idle_state_command = command;
    made_up_aml_name_integer(&idle_aml, "NONE", 0u);
    *out_writes = 0u;
    if (idle_run(test))
    {
        control = idle_counts.idle_control;
        *out_writes = idle_port_writes((uint16_t) command_port, command);
        kernel_test_check(test, idle_table.slots[0].processor == 0u && idle_table.slots[0].count == 0u,
                          "a namespace without processor objects publishes nothing in its slot");
        idle_close(test);
    }
    return control;
}

/**
 * @brief CST_CNT goes to SMI_CMD once, as one byte, when step 0 left the platform in ACPI mode; never in
 *        legacy mode, before step 0, where step 0 could not reach, on a hardware-reduced platform, on a
 *        protected port or past the I/O space; and nothing is to be written when CST_CNT or SMI_CMD is 0, or
 *        no FADT is given.
 */
KERNEL_TEST(cst_control_goes_to_smi_command_once_in_acpi_mode_only)
{
    static const uint8_t written[] = {IDLE_MODE(SWITCHED), IDLE_MODE(ALREADY), IDLE_MODE(NO_LEGACY_MODE)};
    static const uint8_t skipped[] = {IDLE_MODE(FAILED), IDLE_MODE(NOT_RUN), IDLE_MODE(UNREACHABLE),
                                      IDLE_MODE(HARDWARE_REDUCED)};
    uint32_t writes = 0u;

    for (uint32_t mode = 0u; mode < sizeof(written); ++mode)
        kernel_test_check(test,
                          idle_control(test, written[mode], IDLE_COMMAND_PORT, IDLE_CONTROL_COMMAND, true, &writes) ==
                                  IDLE_CONTROL(WRITTEN) &&
                              writes == 1u,
                          "in ACPI mode, CST_CNT is written to SMI_CMD once");
    for (uint32_t mode = 0u; mode < sizeof(skipped); ++mode)
        kernel_test_check(test,
                          idle_control(test, skipped[mode], IDLE_COMMAND_PORT, IDLE_CONTROL_COMMAND, true, &writes) ==
                                  IDLE_CONTROL(SKIPPED) &&
                              writes == 0u,
                          "in legacy mode, before step 0, or where step 0 could not reach, nothing is written");
    kernel_test_check(test,
                      idle_control(test, IDLE_MODE(SWITCHED), 0x20u, IDLE_CONTROL_COMMAND, true, &writes) ==
                              IDLE_CONTROL(SKIPPED) &&
                          writes == 0u,
                      "an SMI_CMD on the 8259's protected port is not written");
    kernel_test_check(test,
                      idle_control(test, IDLE_MODE(SWITCHED), 0x10000u, IDLE_CONTROL_COMMAND, true, &writes) ==
                              IDLE_CONTROL(SKIPPED) &&
                          writes == 0u,
                      "an SMI_CMD past the I/O space is not written");
    kernel_test_check(
        test, idle_control(test, IDLE_MODE(SWITCHED), IDLE_COMMAND_PORT, 0u, true, &writes) == IDLE_CONTROL(ABSENT),
        "a CST_CNT of 0 is nothing to write");
    kernel_test_check(
        test, idle_control(test, IDLE_MODE(SWITCHED), 0u, IDLE_CONTROL_COMMAND, true, &writes) == IDLE_CONTROL(ABSENT),
        "an SMI_CMD of 0 is nothing to write");
    kernel_test_check(test,
                      idle_control(test, IDLE_MODE(SWITCHED), IDLE_COMMAND_PORT, IDLE_CONTROL_COMMAND, false,
                                   &writes) == IDLE_CONTROL(ABSENT),
                      "without a FADT nothing is written");
}

/**
 * @brief Appends Processor (@p name, @p identifier) { @p initialize, and a _CST that increments \@p runs,
 *        then runs @p notifies and returns one MWAIT C1 }.
 */
static void idle_notifying(const char *name, uint8_t identifier, const char *runs, const uint8_t *notifies,
                           uint32_t length, const uint8_t *initialize, uint32_t initialize_length)
{
    static const uint8_t increment[] = {0x75u, '\\'};
    static const uint8_t return_byte = 0xA4u;
    const IdleState_t only = idle_monitor_wait(0x00u, 1u, 1u, 1000u);

    made_up_aml_begin(&idle_method);
    made_up_aml_raw(&idle_method, increment, sizeof(increment));
    made_up_aml_name(&idle_method, runs);
    made_up_aml_raw(&idle_method, notifies, length);
    made_up_aml_raw(&idle_method, &return_byte, 1u);
    idle_states(&idle_method, 1u, &only, 1u);
    made_up_aml_begin(&idle_scope);
    made_up_aml_method(&idle_scope, "_CST", 0x00u, &idle_method);
    if (initialize_length != 0u)
    {
        made_up_aml_begin(&idle_branch);
        made_up_aml_raw(&idle_branch, initialize, initialize_length);
        made_up_aml_method(&idle_scope, "_INI", 0x00u, &idle_branch);
    }
    idle_processor(name, identifier, &idle_scope);
}

/**
 * @brief A Notify 0x81 on a processor queued after its _CST was evaluated evaluates it once more (§3.8 step
 *        9): CPU1's _CST notifies CPU0 and itself, so each runs twice. A Notify 0x81 queued before a
 *        processor's evaluation, by CPU0's _INI for CPU2 or by CPU0's _CST for CPU1, the Notify 0x80 CPU2's
 *        _CST raises on itself, and what the last evaluations raise are dropped: CPU2 runs once.
 */
KERNEL_TEST(a_notify_0x81_after_an_evaluation_evaluates_that_cst_once_more)
{
    static const uint32_t identifiers[] = {1u, 2u, 3u};
    static const uint8_t initialize[] = {0x86u, '\\', 'C', 'P', 'U', '2', 0x0Au, 0x81u};
    static const uint8_t first[] = {0x86u, '\\', 'C', 'P', 'U', '1', 0x0Au, 0x81u};
    static const uint8_t second[] = {0x86u, '\\', 'C', 'P', 'U', '0', 0x0Au, 0x81u,
                                     0x86u, '\\', 'C', 'P', 'U', '1', 0x0Au, 0x81u};
    static const uint8_t third[] = {0x86u, '\\', 'C', 'P', 'U', '2', 0x0Au, 0x80u};

    idle_begin(identifiers, 3u);
    made_up_aml_name_integer(&idle_aml, "C0RN", 0u);
    made_up_aml_name_integer(&idle_aml, "C1RN", 0u);
    made_up_aml_name_integer(&idle_aml, "C2RN", 0u);
    idle_notifying("CPU0", 1u, "C0RN", first, sizeof(first), initialize, sizeof(initialize));
    idle_notifying("CPU1", 2u, "C1RN", second, sizeof(second), NULL, 0u);
    idle_notifying("CPU2", 3u, "C2RN", third, sizeof(third), NULL, 0u);
    if (!idle_run(test))
        return;
    kernel_test_check(test,
                      made_up_root_node(&idle_load, "C0RN")->value.payload == 2u &&
                          made_up_root_node(&idle_load, "C1RN")->value.payload == 2u,
                      "CPU0's and CPU1's _CST run twice: once in step 8, once in step 9");
    kernel_test_check(test, made_up_root_node(&idle_load, "C2RN")->value.payload == 1u,
                      "CPU2's runs once: its Notify 0x81 came before its evaluation, its Notify 0x80 is no change");
    kernel_test_check(test, idle_counts.processor_idle == 5u && idle_counts.processor_idle_changes == 2u,
                      "five evaluations, two of them for a Notify 0x81");
    kernel_test_check(test, idle_load.services.notifications_taken == 8u,
                      "eight Notifies taken: the _INI's, CPU0's twice, CPU1's two twice, CPU2's");
    idle_close(test);
}

/**
 * @brief A _CST that fails when step 9 evaluates it once more publishes nothing in place of what step 8
 *        published: its _CST notifies its own processor 0x81 and divides by zero on its second run.
 */
KERNEL_TEST(a_cst_that_fails_in_step_9_publishes_nothing)
{
    static const uint32_t identifiers[] = {1u};
    static const uint8_t body[] = {0x75u, '\\', 'R',   'U',   'N',   'S',   0x86u, '\\',  'C',   'P',
                                   'U',   '0',  0x0Au, 0x81u, 0xA0u, 0x0Eu, 0x93u, '\\',  'R',   'U',
                                   'N',   'S',  0x0Au, 0x02u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u, 0xA4u};
    const IdleState_t only = idle_monitor_wait(0x00u, 1u, 1u, 1000u);

    idle_begin(identifiers, 1u);
    made_up_aml_name_integer(&idle_aml, "RUNS", 0u);
    made_up_aml_begin(&idle_method);
    made_up_aml_raw(&idle_method, body, sizeof(body));
    idle_states(&idle_method, 1u, &only, 1u);
    made_up_aml_begin(&idle_scope);
    made_up_aml_method(&idle_scope, "_CST", 0x00u, &idle_method);
    idle_processor("CPU0", 1u, &idle_scope);
    if (!idle_run(test))
        return;
    kernel_test_check(test, made_up_root_node(&idle_load, "RUNS")->value.payload == 2u, "the _CST ran twice");
    kernel_test_check(test, idle_table.slots[0].processor != 0u && idle_table.slots[0].count == 0u,
                      "the slot keeps its processor and publishes nothing");
    kernel_test_check(test,
                      idle_counts.processor_idle == 2u && idle_counts.processor_idle_changes == 1u &&
                          idle_counts.processor_idle_failed == 1u,
                      "two evaluations, the second for the Notify 0x81, which failed");
    idle_close(test);
}

/**
 * @brief FNV-1a, 32 bits, over @p length bytes, written here apart from the kernel's fold.
 */
static uint32_t idle_fnv(const uint8_t *bytes, uint32_t length)
{
    uint32_t fold = 0x811C9DC5u;

    for (uint32_t byte = 0u; byte < length; ++byte)
        fold = (fold ^ bytes[byte]) * 0x01000193u;
    return fold;
}

/**
 * @brief The fold of a table built here equals FNV-1a over the bytes §8.7 lists, written out by hand:
 *        the slot count, then per slot whether it was evaluated, its count and its skipped states, then each
 *        state's space, width, offset, access size, address, type, latency and power, never its node.
 */
KERNEL_TEST(the_fold_covers_every_published_field)
{
    static AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t table;
    static const uint8_t bytes[] = {0x02u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x00u,
                                    0x00u, 0x01u, 0x08u, 0x00u, 0x01u, 0x15u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                    0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x68u, 0x00u, 0x00u, 0x00u, 0x5Eu, 0x01u, 0x00u,
                                    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};

    memset(&table, 0, sizeof(table));
    table.slot_count = 2u;
    table.slots[0].processor = 0x1234u;
    table.slots[0].count = 1u;
    table.slots[0].skipped = 3u;
    table.slots[0].states[0] = idle_port(0x415u, 1u, 2u, 0x68u, 350u);
    kernel_test_check(
        test, advanced_configuration_and_power_interface_processor_idle_fold(&table) == idle_fnv(bytes, sizeof(bytes)),
        "the fold is FNV-1a over the bytes written out by hand");
}

/**
 * @brief Folds the table of one processor whose single state has latency @p latency.
 */
static uint32_t idle_fold_of(KernelTest_t *test, uint32_t latency)
{
    static const uint32_t identifiers[] = {1u};
    const IdleState_t only = idle_port(0x415u, 0u, 2u, latency, 350u);
    uint32_t fold = 0u;

    idle_begin(identifiers, 1u);
    made_up_aml_begin(&idle_scope);
    idle_static(&idle_scope, 1u, &only, 1u);
    idle_processor("CPU0", 1u, &idle_scope);
    if (!idle_run(test))
        return 0u;
    fold = advanced_configuration_and_power_interface_processor_idle_fold(&idle_table);
    idle_close(test);
    return fold;
}

/**
 * @brief The control of §8.7: the same _CST folds the same, and one whose latency changed by one folds
 *        differently.
 */
KERNEL_TEST(a_latency_changed_by_one_changes_the_fold)
{
    const uint32_t fold = idle_fold_of(test, 100u);

    kernel_test_check(test, fold == idle_fold_of(test, 100u), "the same table folds the same");
    kernel_test_check(test, fold != idle_fold_of(test, 101u), "a latency changed by one folds differently");
    kernel_test_measure_hexadecimal(test, "idle_fold", fold);
}

/**
 * @brief The iterator reads a template in place: a small descriptor and a large one skipped by their
 *        lengths, a Generic Register decoded, the End Tag ending it; a descriptor that runs past the bytes is
 *        malformed, the end of the bytes ends it, and a Generic Register shorter than 12 bytes decodes nothing.
 */
KERNEL_TEST(a_resource_template_is_read_in_place)
{
    static const uint8_t template_bytes[] = {0x22u, 0x01u, 0x00u, 0x86u, 0x09u, 0x00u, 0x01u, 0x00u, 0x00u,
                                             0xD0u, 0xFEu, 0x00u, 0x10u, 0x00u, 0x00u, 0x82u, 0x0Cu, 0x00u,
                                             0x01u, 0x08u, 0x00u, 0x01u, 0x15u, 0x04u, 0x00u, 0x00u, 0x00u,
                                             0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x22u, 0x02u, 0x00u};
    static const uint8_t cut_large[] = {0x82u, 0x0Cu};
    static const uint8_t past[] = {0x82u, 0x0Cu, 0x00u, 0x7Fu};
    static const uint8_t short_register[] = {0x82u, 0x02u, 0x00u, 0x7Fu, 0x01u};
    static const uint8_t no_end[] = {0x22u, 0x01u, 0x00u};
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t iterator;
    AdvancedConfigurationAndPowerInterfaceResource_t resource;
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t entry;
    uint8_t names[4] = {0};
    uint32_t count = 0u;

    advanced_configuration_and_power_interface_resources_begin(&iterator, template_bytes, sizeof(template_bytes));
    while (count < 4u && advanced_configuration_and_power_interface_resources_next(&iterator, &resource) ==
                             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_DESCRIPTOR)
        names[count++] = resource.name;
    kernel_test_check(test, count == 3u && names[0] == 0x04u && names[1] == 0x86u && names[2] == 0x82u,
                      "an IRQ, a Memory32Fixed and a Generic Register, then the End Tag ends it");
    kernel_test_check(test, iterator.offset == 30u, "the iterator stays at the End Tag, the bytes after it unread");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_resources_generic_register(&resource, &entry) &&
                          entry.space == 0x01u && entry.bit_width == 8u && entry.bit_offset == 0u &&
                          entry.access_size == 1u && entry.address == 0x415u,
                      "the Generic Register decodes SystemIO, 8 bits, port 0x415");
    advanced_configuration_and_power_interface_resources_begin(&iterator, cut_large, sizeof(cut_large));
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_resources_next(&iterator, &resource) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_MALFORMED,
                      "a large descriptor cut inside its header is malformed");
    advanced_configuration_and_power_interface_resources_begin(&iterator, past, sizeof(past));
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_resources_next(&iterator, &resource) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_MALFORMED,
                      "a descriptor whose data runs past the bytes is malformed");
    advanced_configuration_and_power_interface_resources_begin(&iterator, short_register, sizeof(short_register));
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_resources_next(&iterator, &resource) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_DESCRIPTOR &&
                          !advanced_configuration_and_power_interface_resources_generic_register(&resource, &entry),
                      "a Generic Register of 2 bytes decodes nothing");
    advanced_configuration_and_power_interface_resources_begin(&iterator, no_end, sizeof(no_end));
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_resources_next(&iterator, &resource) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_DESCRIPTOR &&
                          advanced_configuration_and_power_interface_resources_next(&iterator, &resource) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_END,
                      "the end of the bytes ends a template without an End Tag");
}

/**
 * @brief A Package element read for a consumer gives the element, or the object a name it holds names;
 *        no Package, an index past the end, an Uninitialized element and a name of a method are refused.
 */
KERNEL_TEST(a_package_element_holding_a_name_reads_what_it_names)
{
    static const uint32_t identifiers[] = {1u};
    static const uint8_t package[] = {0x08u, 'P', 'K', 'G', '0', 0x12u, 0x0Cu, 0x04u, 0x0Au,
                                      0x07u, 'N', 'A', 'M', '1', 'M',   'T',   'H',   '1'};
    static const uint8_t method[] = {0x14u, 0x06u, 'M', 'T', 'H', '1', 0x00u};
    const IdleValue_t integer = {.kind = IDLE_VALUE(INTEGER), .payload = 1u};
    IdleValue_t root;
    IdleValue_t element;

    idle_begin(identifiers, 1u);
    made_up_aml_name_integer(&idle_aml, "NAM1", 0x2Au);
    made_up_aml_raw(&idle_aml, method, sizeof(method));
    made_up_aml_raw(&idle_aml, package, sizeof(package));
    if (!idle_run(test))
        return;
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_evaluate(
                               &idle_load, "\\PKG0", NULL, 0u, &root) == IDLE_STATUS(OK) &&
                               root.kind == IDLE_VALUE(PACKAGE),
                           "PKG0 reads as a Package"))
    {
        idle_close(test);
        return;
    }
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_package_element(
                          &idle_load, &root, 0u, &element) == IDLE_STATUS(OK) &&
                          element.kind == IDLE_VALUE(INTEGER) && element.payload == 7u,
                      "element 0 is the Integer 7");
    advanced_configuration_and_power_interface_machine_language_value_release(&idle_load.heap, &idle_load.space,
                                                                              &element);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_package_element(
                          &idle_load, &root, 1u, &element) == IDLE_STATUS(OK) &&
                          element.kind == IDLE_VALUE(INTEGER) && element.payload == 0x2Au,
                      "element 1, the name NAM1, reads NAM1's Integer");
    advanced_configuration_and_power_interface_machine_language_value_release(&idle_load.heap, &idle_load.space,
                                                                              &element);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_package_element(
                          &idle_load, &root, 2u, &element) == IDLE_STATUS(TYPE_MISMATCH),
                      "element 2, the name of a method, is refused");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_package_element(
                          &idle_load, &root, 3u, &element) == IDLE_STATUS(TYPE_MISMATCH),
                      "element 3, Uninitialized, is refused");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_package_element(
                          &idle_load, &root, 4u, &element) == IDLE_STATUS(INDEX_OUT_OF_RANGE),
                      "element 4, past the end, is refused");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_package_element(
                          &idle_load, &integer, 0u, &element) == IDLE_STATUS(TYPE_MISMATCH),
                      "a value that is no Package is refused");
    advanced_configuration_and_power_interface_machine_language_value_release(&idle_load.heap, &idle_load.space, &root);
    idle_close(test);
}
