#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_initialization, KERNEL_TEST_STAGE_INITIALIZATION);

#define INITIALIZATION_PCI_CONFIG    0x02u /**< The RegionSpace byte of PCI_Config. */
#define INITIALIZATION_SYSTEM_MEMORY 0x00u /**< The RegionSpace byte of SystemMemory. */
#define INITIALIZATION_SYSTEM_IO     0x01u /**< The RegionSpace byte of SystemIO. */
#define INITIALIZATION_STATUS(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t initialization_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t initialization_load;
static AdvancedConfigurationAndPowerInterfaceInitialization_t initialization_counts;
static AdvancedConfigurationAndPowerInterfaceMadtState_t initialization_madt;
static AdvancedConfigurationAndPowerInterfaceInitializationMachine_t initialization_machine;
static uint8_t initialization_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static MadeUpAml_t initialization_aml;
static MadeUpAml_t initialization_outer;
static MadeUpAml_t initialization_inner;
static MadeUpAml_t initialization_method_body;

/**
 * @brief Opens the simulated host and empties the AML, which starts with Name (TRAC, 0): the trace
 *        each made-up method appends its hexadecimal digits to; the machine has an empty MADT and no
 *        MONITOR/MWAIT.
 */
static void initialization_begin(void)
{
    memset(&initialization_madt, 0, sizeof(initialization_madt));
    initialization_machine.madt = &initialization_madt;
    initialization_machine.monitor_wait = false;
    advanced_configuration_and_power_interface_simulated_host_open(&initialization_host);
    made_up_aml_begin(&initialization_aml);
    made_up_aml_name_integer(&initialization_aml, "TRAC", 0u);
}

/**
 * @brief Loads the AML written as a DSDT of revision 2, then runs the initialisation over it when
 *        @p initialize holds.
 */
static bool initialization_run(KernelTest_t *test, bool initialize)
{
    if (!kernel_test_check(test, !initialization_aml.overflowed, "the made-up AML fits") ||
        !made_up_load_on(test, &initialization_load, &initialization_host.host, initialization_table, 2u,
                         initialization_aml.bytes, initialization_aml.length))
        return false;
    memset(&initialization_counts, 0, sizeof(initialization_counts));
    if (initialize)
        advanced_configuration_and_power_interface_initialize_namespace(&initialization_load, &initialization_machine,
                                                                        &initialization_counts);
    return true;
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t initialization_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        made_up_root_node(&initialization_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Folds the eight little-endian bytes of a trace with FNV-1a, 32 bits.
 */
static uint32_t initialization_fold(uint64_t trace)
{
    uint32_t fold = 0x811C9DC5u;

    for (uint32_t byte = 0u; byte < 8u; ++byte)
        fold = (fold ^ (uint8_t) (trace >> (8u * byte))) * 0x01000193u;
    return fold;
}

/**
 * @brief Evaluates the method at @p path, which takes no argument, and gives back its status.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t initialization_call(const char *path)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&initialization_load, path, NULL, 0u,
                                                                             &result);

    advanced_configuration_and_power_interface_machine_language_value_release(&initialization_load.heap,
                                                                              &initialization_load.space, &result);
    return status;
}

/**
 * @brief Checks that the evaluations left nothing behind, then closes the load.
 */
static void initialization_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation =
        &initialization_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && evaluation->block_count == 0u &&
                          initialization_load.temporary_count == 0u,
                      "every evaluation leaves no frame, value, operator, block or temporary node behind");
    kernel_test_check(test, initialization_load.heap.stale_handles == 0u, "no retain or release met a dead object");
    advanced_configuration_and_power_interface_machine_language_close(&initialization_load);
    advanced_configuration_and_power_interface_simulated_host_close(&initialization_host);
}

/**
 * @brief Appends Store (Add (Multiply (\TRAC, 0x10), @p digit), \TRAC): @p digit, from 1 to 0xF,
 *        becomes the trace's last hexadecimal digit.
 */
static void initialization_trace(MadeUpAml_t *aml, uint64_t digit)
{
    static const uint8_t head[] = {0x70u, 0x72u, 0x77u, '\\', 'T', 'R', 'A', 'C', 0x0Au, 0x10u, 0x00u};
    static const uint8_t tail[] = {0x00u, '\\', 'T', 'R', 'A', 'C'};

    made_up_aml_raw(aml, head, sizeof(head));
    made_up_aml_integer(aml, digit);
    made_up_aml_raw(aml, tail, sizeof(tail));
}

/**
 * @brief Appends Method (_INI) that traces @p digit.
 */
static void initialization_ini(MadeUpAml_t *aml, uint64_t digit)
{
    made_up_aml_begin(&initialization_method_body);
    initialization_trace(&initialization_method_body, digit);
    made_up_aml_method(aml, "_INI", 0x00u, &initialization_method_body);
}

/**
 * @brief Appends Method (@p name, @p arguments) whose body is the @p length bytes of @p body.
 */
static void initialization_method(MadeUpAml_t *aml, const char *name, uint8_t arguments, const uint8_t *body,
                                  uint32_t length)
{
    made_up_aml_begin(&initialization_method_body);
    made_up_aml_raw(&initialization_method_body, body, length);
    made_up_aml_method(aml, name, arguments, &initialization_method_body);
}

/**
 * @brief Appends Method (_STA) { Return (@p status) }.
 */
static void initialization_status(MadeUpAml_t *aml, uint64_t status)
{
    static const uint8_t return_byte = 0xA4u;

    made_up_aml_begin(&initialization_method_body);
    made_up_aml_raw(&initialization_method_body, &return_byte, 1u);
    made_up_aml_integer(&initialization_method_body, status);
    made_up_aml_method(aml, "_STA", 0x00u, &initialization_method_body);
}

/**
 * @brief Appends Method (@p name, @p arguments) { Divide (One, Zero, Zero, Zero) }, which fails with
 *        divide_by_zero; with @p returns, it returns the quotient.
 */
static void initialization_failing(MadeUpAml_t *aml, const char *name, uint8_t arguments, bool returns)
{
    static const uint8_t divide[] = {0xA4u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u};

    initialization_method(aml, name, arguments, returns ? divide : divide + 1, returns ? 6u : 5u);
}

/**
 * @brief Appends Method (_REG, 2), which traces its two arguments, Arg0 then Arg1.
 */
static void initialization_registration(MadeUpAml_t *aml)
{
    static const uint8_t body[] = {0x70u, 0x72u, 0x77u, 0x72u, 0x77u, '\\',  'T',   'R',  'A', 'C', 0x0Au, 0x10u, 0x00u,
                                   0x68u, 0x00u, 0x0Au, 0x10u, 0x00u, 0x69u, 0x00u, '\\', 'T', 'R', 'A',   'C'};

    initialization_method(aml, "_REG", 0x02u, body, sizeof(body));
}

/**
 * @brief Appends Device (@p name) { @p body } with an _INI that traces @p digit before @p body, or none
 *        when @p digit is 0.
 */
static void initialization_device(MadeUpAml_t *aml, const char *name, uint64_t digit, const MadeUpAml_t *body)
{
    static MadeUpAml_t device_body;

    made_up_aml_begin(&device_body);
    if (digit != 0u)
        initialization_ini(&device_body, digit);
    if (body != NULL)
        made_up_aml_raw(&device_body, body->bytes, body->length);
    device_body.overflowed = device_body.overflowed || (body != NULL && body->overflowed);
    made_up_aml_device(aml, name, &device_body);
}

/**
 * @brief Appends Device (@p name) { _STA returning @p status; _INI tracing @p digit; Device (CHLD) {
 *        _INI tracing @p child_digit } }.
 */
static void initialization_device_with_status(MadeUpAml_t *aml, const char *name, uint64_t status, uint64_t digit,
                                              uint64_t child_digit)
{
    made_up_aml_begin(&initialization_inner);
    initialization_device(&initialization_inner, "CHLD", child_digit, NULL);
    made_up_aml_begin(&initialization_outer);
    initialization_status(&initialization_outer, status);
    initialization_ini(&initialization_outer, digit);
    made_up_aml_raw(&initialization_outer, initialization_inner.bytes, initialization_inner.length);
    made_up_aml_device(aml, name, &initialization_outer);
}

/**
 * @brief Appends a NameString of the root, \ then the four characters @p prefix then @p suffix make.
 */
static void initialization_root_name(MadeUpAml_t *aml, char prefix, const char *suffix)
{
    const uint8_t name[5] = {'\\', (uint8_t) prefix, (uint8_t) suffix[0], (uint8_t) suffix[1], (uint8_t) suffix[2]};

    made_up_aml_raw(aml, name, sizeof(name));
}

/**
 * @brief Appends the four root Names, all Zero, an _OSC recorder of @p record keeps its calls in: @p record
 *        then UID, REV, CNT and CAP.
 */
static void initialization_capabilities_names(MadeUpAml_t *aml, char record)
{
    static const char *const suffixes[] = {"UID", "REV", "CNT", "CAP"};

    for (uint32_t index = 0u; index < 4u; ++index)
    {
        const char name[5] = {record, suffixes[index][0], suffixes[index][1], suffixes[index][2], '\0'};

        made_up_aml_name_integer(aml, name, 0u);
    }
}

/**
 * @brief Appends Method (_OSC, 4) { CopyObject Arg0, Arg1 and Arg2 to \@p record UID, REV and CNT;
 *        CreateDWordField (Arg3, 0, CDW1) and (Arg3, 4, CDW2); trace CDW1 + @p digit; \@p record CAP |=
 *        CDW2; CDW1 |= @p error_bits; Return (Arg3) }: the query traces @p digit + 1, the commit @p digit.
 */
static void initialization_capabilities_recorder(MadeUpAml_t *aml, char record, uint8_t digit, uint8_t error_bits)
{
    static const uint8_t first_field[] = {0x8Au, 0x6Bu, 0x00u, 'C', 'D', 'W', '1'};
    static const uint8_t second_field[] = {0x8Au, 0x6Bu, 0x0Au, 0x04u, 'C', 'D', 'W', '2'};
    static const uint8_t trace_head[] = {0x70u, 0x72u, 0x77u, '\\', 'T', 'R', 'A', 'C',  0x0Au,
                                         0x10u, 0x00u, 0x72u, 'C',  'D', 'W', '1', 0x0Au};
    static const uint8_t trace_tail[] = {0x00u, 0x00u, '\\', 'T', 'R', 'A', 'C'};
    static const uint8_t or_byte = 0x7Du;
    static const uint8_t refuse_head[] = {0x7Du, 'C', 'D', 'W', '1', 0x0Au};
    static const uint8_t refuse_tail[] = {'C', 'D', 'W', '1'};
    static const uint8_t give_back[] = {0xA4u, 0x6Bu};
    static const char *const kept[] = {"UID", "REV", "CNT"};

    made_up_aml_begin(&initialization_method_body);
    for (uint8_t argument = 0u; argument < 3u; ++argument)
    {
        const uint8_t copy[] = {0x9Du, (uint8_t) (0x68u + argument)};

        made_up_aml_raw(&initialization_method_body, copy, sizeof(copy));
        initialization_root_name(&initialization_method_body, record, kept[argument]);
    }
    made_up_aml_raw(&initialization_method_body, first_field, sizeof(first_field));
    made_up_aml_raw(&initialization_method_body, second_field, sizeof(second_field));
    made_up_aml_raw(&initialization_method_body, trace_head, sizeof(trace_head));
    made_up_aml_raw(&initialization_method_body, &digit, 1u);
    made_up_aml_raw(&initialization_method_body, trace_tail, sizeof(trace_tail));
    made_up_aml_raw(&initialization_method_body, &or_byte, 1u);
    initialization_root_name(&initialization_method_body, record, "CAP");
    made_up_aml_raw(&initialization_method_body, second_field + 4, 4u);
    initialization_root_name(&initialization_method_body, record, "CAP");
    if (error_bits != 0u)
    {
        made_up_aml_raw(&initialization_method_body, refuse_head, sizeof(refuse_head));
        made_up_aml_raw(&initialization_method_body, &error_bits, 1u);
        made_up_aml_raw(&initialization_method_body, refuse_tail, sizeof(refuse_tail));
    }
    made_up_aml_raw(&initialization_method_body, give_back, sizeof(give_back));
    made_up_aml_method(aml, "_OSC", 0x04u, &initialization_method_body);
}

/**
 * @brief Appends Method (_PDC, 1) { CopyObject (Arg0, \@p record PDC); trace @p digit }.
 */
static void initialization_driver_capabilities_recorder(MadeUpAml_t *aml, char record, uint64_t digit)
{
    static const uint8_t copy[] = {0x9Du, 0x68u};

    made_up_aml_begin(&initialization_method_body);
    made_up_aml_raw(&initialization_method_body, copy, sizeof(copy));
    initialization_root_name(&initialization_method_body, record, "PDC");
    initialization_trace(&initialization_method_body, digit);
    made_up_aml_method(aml, "_PDC", 0x01u, &initialization_method_body);
}

/**
 * @brief Appends Name (_HID, @p identifier), a String of eight characters.
 */
static void initialization_hardware_identifier(MadeUpAml_t *aml, const char *identifier)
{
    static const uint8_t head[] = {0x08u, '_', 'H', 'I', 'D', 0x0Du};
    static const uint8_t end = 0x00u;

    made_up_aml_raw(aml, head, sizeof(head));
    made_up_aml_raw(aml, (const uint8_t *) identifier, 8u);
    made_up_aml_raw(aml, &end, 1u);
}

/**
 * @brief The 16 bytes ToUUID makes of @p text, "aabbccdd-eeff-gghh-iijj-kkllmmnnoopp": aa to hh land at
 *        offsets 3, 2, 1, 0, 5, 4, 7 and 6, ii to pp in the order written (ACPI 6.6 §19.6.144).
 */
static void initialization_uuid(const char *text, uint8_t out_bytes[16])
{
    static const uint8_t written_at[16] = {3u, 2u, 1u, 0u, 5u, 4u, 7u, 6u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u};
    uint8_t written[16] = {0};
    uint32_t digits = 0u;

    for (const char *character = text; *character != '\0' && digits < 32u; ++character)
    {
        if (*character == '-')
            continue;

        const uint8_t digit =
            (*character <= '9') ? (uint8_t) (*character - '0') : (uint8_t) ((*character | 0x20) - 'a' + 10);

        written[digits / 2u] = (uint8_t) ((digits % 2u == 0u) ? (digit << 4u) : (written[digits / 2u] | digit));
        ++digits;
    }
    for (uint32_t index = 0u; index < 16u; ++index)
        out_bytes[written_at[index]] = written[index];
}

/**
 * @brief Whether the Name at the root called @p name holds a Buffer of the @p length bytes at @p bytes.
 */
static bool initialization_buffer_holds(const char *name, const uint8_t *bytes, uint32_t length)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        made_up_root_node(&initialization_load, name);
    const uint8_t *held = NULL;
    uint32_t held_length = 0u;
    uint8_t scratch[8];

    return node != NULL &&
           node->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER &&
           advanced_configuration_and_power_interface_machine_language_value_bytes(
               &initialization_load.heap, &node->value, 64u, scratch, &held, &held_length) ==
               INITIALIZATION_STATUS(OK) &&
           held_length == length && memcmp(held, bytes, length) == 0;
}

/**
 * @brief Gives the CPU slots of the machine the ACPI Processor UIDs @p identifiers, in that order.
 */
static void initialization_slots(const uint32_t *identifiers, uint32_t count)
{
    initialization_madt.processor_count = count;
    for (uint32_t slot = 0u; slot < count; ++slot)
        initialization_madt.processors[slot].processor_unique_identifier = identifiers[slot];
}

/**
 * @brief \_INI and \_SB._INI run first, then _REG (PCI_Config, 1), then the _INI of every device,
 *        processor and thermal zone in declaration order, children after their parent (§3.8 steps 2
 *        to 4): the trace holds each in that order.
 */
KERNEL_TEST(the_initialization_runs_in_the_order_firmware_expects)
{
    static MadeUpAml_t system_bus;

    initialization_begin();
    initialization_ini(&initialization_aml, 0xAu);
    made_up_aml_begin(&system_bus);
    initialization_ini(&system_bus, 0xBu);
    made_up_aml_begin(&initialization_inner);
    initialization_device(&initialization_inner, "DEVB", 0xDu, NULL);
    initialization_device(&system_bus, "DEVA", 0xCu, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_region(&initialization_inner, "PCFG", INITIALIZATION_PCI_CONFIG, 0u, 0x100u);
    initialization_registration(&initialization_inner);
    initialization_device(&system_bus, "DEVC", 0xEu, &initialization_inner);
    made_up_aml_scope(&initialization_aml, "_SB_", &system_bus);
    made_up_aml_begin(&initialization_inner);
    initialization_ini(&initialization_inner, 7u);
    made_up_aml_processor(&initialization_aml, "CPU0", 0u, 0x810u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_ini(&initialization_inner, 8u);
    made_up_aml_thermal_zone(&initialization_aml, "TZ00", &initialization_inner);
    if (!initialization_run(test, true))
        return;
    kernel_test_measure_hexadecimal(test, "order_fold", initialization_fold(initialization_integer("TRAC")));
    kernel_test_check(test, initialization_integer("TRAC") == 0xAB21CDE78ull,
                      "\\_INI (A), \\_SB._INI (B), _REG (2, 1), then DEVA (C), DEVB (D), DEVC (E), CPU0 (7), TZ00 (8)");
    kernel_test_check(test,
                      initialization_counts.initializations == 7u && initialization_counts.initialization_failed == 0u,
                      "seven _INI run, none fails");
    kernel_test_check(
        test, initialization_counts.registrations == 1u && initialization_counts.region_registration_failed == 0u,
        "one _REG runs");
    kernel_test_check(test, initialization_counts.devices == 7u && initialization_counts.status_evaluations == 0u,
                      "seven devices, processors and thermal zones, \\_SB_ and \\_TZ_ included, none with a _STA, "
                      "which reads as 0x0F");
    initialization_close(test);
}

/**
 * @brief A device present runs its _INI and its children's, present but not functioning included;
 *        one functioning but not present skips its own _INI and runs its children's; one neither
 *        skips its subtree; a _STA that is a Name is read as a method's result is (§3.8 step 4).
 */
KERNEL_TEST(the_status_of_a_device_decides_its_initialization_and_its_childrens)
{
    initialization_begin();
    initialization_device_with_status(&initialization_aml, "DPRS", 0x01u, 1u, 2u);
    initialization_device_with_status(&initialization_aml, "DFUN", 0x08u, 3u, 4u);
    initialization_device_with_status(&initialization_aml, "DNON", 0x00u, 5u, 6u);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_name_integer(&initialization_outer, "_STA", 0u);
    initialization_ini(&initialization_outer, 7u);
    initialization_device(&initialization_aml, "DNAM", 0u, &initialization_outer);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("TRAC") == 0x124u,
                      "DPRS (1) and its child (2), DFUN's child (4); nothing of DNON, nor DNAM's _INI");
    kernel_test_check(test, initialization_counts.status_evaluations == 4u && initialization_counts.status_failed == 0u,
                      "four _STA evaluated, the Name included");
    kernel_test_check(test, initialization_counts.devices == 9u,
                      "nine devices in the namespace, \\_SB_ and \\_TZ_ included");
    initialization_close(test);
}

/**
 * @brief The _STA results ACPI 6.6 §6.3.7 calls invalid, bit 0 clear with bit 1 set, and bit 0 set with
 *        bit 1 clear and bit 3 set, are counted and walked by bits 0 and 3 alone, as its Table 6.67 says:
 *        0x09 and 0x0D run their _INI and their child's, 0x0A walks its child without _INI, and 0x02
 *        skips its subtree (§3.8 step 4).
 */
KERNEL_TEST(a_status_acpi_calls_invalid_is_counted_and_walked_by_presence_and_function)
{
    initialization_begin();
    initialization_device_with_status(&initialization_aml, "DIN1", 0x02u, 1u, 2u);
    initialization_device_with_status(&initialization_aml, "DIN2", 0x09u, 3u, 4u);
    initialization_device_with_status(&initialization_aml, "DIN3", 0x0Du, 7u, 8u);
    initialization_device_with_status(&initialization_aml, "DIN4", 0x0Au, 9u, 0xAu);
    initialization_device_with_status(&initialization_aml, "DVAL", 0x0Bu, 5u, 6u);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("TRAC") == 0x3478A56u,
                      "DIN2 (3, 4) and DIN3 (7, 8) run with their children, DIN4 only its child (A), DIN1 nothing");
    kernel_test_check(test, initialization_counts.status_invalid == 4u && initialization_counts.status_failed == 0u,
                      "four invalid results, counted apart from failures");
    initialization_close(test);
}

/**
 * @brief A _STA that divides by zero gives its device no _INI, still walks its children, and is counted
 *        under its status and named as the first failure; one that gives a Package fails too, and an
 *        empty Buffer reads as 0, the conversion ACPI forbids counted (§3.8, the failure table, §5.2).
 */
KERNEL_TEST(a_status_that_fails_skips_the_initialization_but_not_the_children)
{
    static const uint8_t package_status[] = {0x08u, '_', 'S', 'T', 'A', 0x12u, 0x02u, 0x00u};
    static const uint8_t empty_status[] = {0x08u, '_', 'S', 'T', 'A', 0x11u, 0x02u, 0x00u};

    initialization_begin();
    made_up_aml_begin(&initialization_inner);
    initialization_device(&initialization_inner, "CHLD", 2u, NULL);
    made_up_aml_begin(&initialization_outer);
    initialization_failing(&initialization_outer, "_STA", 0x00u, true);
    initialization_ini(&initialization_outer, 1u);
    made_up_aml_raw(&initialization_outer, initialization_inner.bytes, initialization_inner.length);
    made_up_aml_device(&initialization_aml, "DDIV", &initialization_outer);
    initialization_device(&initialization_aml, "NEXT", 3u, NULL);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_raw(&initialization_outer, package_status, sizeof(package_status));
    initialization_ini(&initialization_outer, 4u);
    initialization_device(&initialization_aml, "DPKG", 0u, &initialization_outer);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_raw(&initialization_outer, empty_status, sizeof(empty_status));
    initialization_ini(&initialization_outer, 5u);
    initialization_device(&initialization_aml, "DBUF", 0u, &initialization_outer);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("TRAC") == 0x23u,
                      "DDIV's child (2) and NEXT (3) run, not DDIV (1), DPKG (4) or DBUF (5)");
    kernel_test_check(test, initialization_load.evaluation.forbidden_conversions == 1u,
                      "DBUF's empty Buffer reads as 0, a conversion ACPI forbids, counted");
    kernel_test_check(test,
                      initialization_counts.status_failed == 2u &&
                          initialization_counts.statuses[INITIALIZATION_STATUS(DIVIDE_BY_ZERO)] == 1u &&
                          initialization_counts.statuses[INITIALIZATION_STATUS(TYPE_MISMATCH)] == 1u,
                      "the failures are counted under divide_by_zero, and type_mismatch for a _STA that is a Package");

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *failed =
        advanced_configuration_and_power_interface_machine_language_namespace_node(
            &initialization_load.space, initialization_counts.first_failure_node);

    kernel_test_check(test,
                      failed != NULL &&
                          failed->name == advanced_configuration_and_power_interface_machine_language_name_segment(
                                              (const uint8_t *) "_STA", 0u) &&
                          initialization_counts.first_failure == INITIALIZATION_STATUS(DIVIDE_BY_ZERO),
                      "the first failure names DDIV's _STA and its status");

    char path[12];

    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_namespace_path(
                          &initialization_load.space, initialization_counts.first_failure_node, path, sizeof(path)) ==
                              10u &&
                          memcmp(path, "\\DDIV._STA", 11u) == 0,
                      "its path is \\DDIV._STA");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_namespace_path(
                          &initialization_load.space, initialization_counts.first_failure_node, path, 10u) == 0u &&
                          memcmp(path, "\\DDIV._STA", 11u) == 0,
                      "a buffer one byte short of the path and its NUL is left as it was");
    initialization_close(test);
}

/**
 * @brief An _INI that fails, at the root, in \_SB_ or in a device, is counted and the sequence goes on:
 *        the _REG of step 3 runs, and the walk still descends into the failing device's children
 *        (§3.8, the failure table).
 */
KERNEL_TEST(an_initialization_that_fails_is_counted_and_the_sequence_goes_on)
{
    static MadeUpAml_t system_bus;

    initialization_begin();
    initialization_failing(&initialization_aml, "_INI", 0x00u, false);
    made_up_aml_begin(&system_bus);
    initialization_failing(&system_bus, "_INI", 0x00u, false);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_region(&initialization_inner, "PCFG", INITIALIZATION_PCI_CONFIG, 0u, 0x100u);
    initialization_registration(&initialization_inner);
    initialization_device(&system_bus, "DEVA", 3u, &initialization_inner);
    made_up_aml_scope(&initialization_aml, "_SB_", &system_bus);
    made_up_aml_begin(&initialization_inner);
    initialization_device(&initialization_inner, "CHLD", 4u, NULL);
    made_up_aml_begin(&initialization_outer);
    initialization_failing(&initialization_outer, "_INI", 0x00u, false);
    made_up_aml_raw(&initialization_outer, initialization_inner.bytes, initialization_inner.length);
    made_up_aml_device(&initialization_aml, "DBAD", &initialization_outer);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("TRAC") == 0x2134u,
                      "_REG (2, 1), DEVA (3) and DBAD's child (4) run after three failing _INI");
    kernel_test_check(test,
                      initialization_counts.initializations == 5u && initialization_counts.initialization_failed == 3u,
                      "five _INI run, three of them fail");

    char path[8];

    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_namespace_path(
                          &initialization_load.space, initialization_counts.first_failure_node, path, sizeof(path)) ==
                              5u &&
                          memcmp(path, "\\_INI", 6u) == 0,
                      "the first failure is \\_INI's, which the later ones do not replace");
    initialization_close(test);
}

/**
 * @brief A _REG that fails is counted, and the next scope still registers (§3.8, the failure table).
 */
KERNEL_TEST(a_registration_that_fails_is_counted_and_the_next_scope_registers)
{
    initialization_begin();
    made_up_aml_begin(&initialization_inner);
    made_up_aml_region(&initialization_inner, "PCFG", INITIALIZATION_PCI_CONFIG, 0u, 0x100u);
    initialization_failing(&initialization_inner, "_REG", 0x02u, false);
    initialization_device(&initialization_aml, "DRG1", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_region(&initialization_inner, "PCFG", INITIALIZATION_PCI_CONFIG, 0u, 0x100u);
    initialization_registration(&initialization_inner);
    initialization_device(&initialization_aml, "DRG2", 0u, &initialization_inner);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("TRAC") == 0x21u, "DRG2's _REG runs after DRG1's fails");
    kernel_test_check(
        test, initialization_counts.registrations == 2u && initialization_counts.region_registration_failed == 1u,
        "two _REG run, one fails");
    initialization_close(test);
}

/**
 * @brief _REG (PCI_Config, 1) runs once in each scope that declares a PCI_Config region, the root
 *        included, however many it declares; never for a scope of SystemMemory and SystemIO regions
 *        alone, and never from a scope above the region's (§3.8 step 3).
 */
KERNEL_TEST(a_registration_runs_once_per_scope_of_pci_config_regions)
{
    initialization_begin();
    made_up_aml_region(&initialization_aml, "RPCI", INITIALIZATION_PCI_CONFIG, 0u, 0x100u);
    initialization_registration(&initialization_aml);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_region(&initialization_inner, "PCF1", INITIALIZATION_PCI_CONFIG, 0u, 0x10u);
    made_up_aml_region(&initialization_inner, "MEM1", INITIALIZATION_SYSTEM_MEMORY, 0x1000u, 0x10u);
    made_up_aml_region(&initialization_inner, "PCF2", INITIALIZATION_PCI_CONFIG, 0x10u, 0x10u);
    made_up_aml_region(&initialization_inner, "IO_1", INITIALIZATION_SYSTEM_IO, 0x80u, 1u);
    initialization_registration(&initialization_inner);
    initialization_device(&initialization_aml, "DTWO", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_region(&initialization_inner, "MEM2", INITIALIZATION_SYSTEM_MEMORY, 0x2000u, 0x10u);
    made_up_aml_region(&initialization_inner, "IO_2", INITIALIZATION_SYSTEM_IO, 0x81u, 1u);
    initialization_registration(&initialization_inner);
    initialization_device(&initialization_aml, "DMEM", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_region(&initialization_inner, "PCF3", INITIALIZATION_PCI_CONFIG, 0u, 0x10u);
    made_up_aml_begin(&initialization_outer);
    initialization_registration(&initialization_outer);
    initialization_device(&initialization_outer, "DCHL", 0u, &initialization_inner);
    initialization_device(&initialization_aml, "DPAR", 0u, &initialization_outer);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("TRAC") == 0x2121u,
                      "the root's _REG, then DTWO's once; neither DMEM's nor DPAR's");
    kernel_test_check(test, initialization_counts.registrations == 2u, "two _REG run");
    initialization_close(test);
}

/**
 * @brief Once the initialisation has registered, a PCI_Config region a method creates in a scope
 *        with a _REG gets _REG (PCI_Config, 1) at once, before the method's next statement, and a _REG
 *        that fails there is dropped, counted; one created before, one in the method's own scope or one
 *        of SystemMemory gets none, and a _REG that does not take two arguments is not called, counted
 *        as failed (§5.3).
 */
KERNEL_TEST(a_region_a_method_creates_after_initialization_registers_at_once)
{
    static const uint8_t creates[] = {0x5Bu, 0x80u, '^', 'D', 'Y', 'N', 'R',  0x02u, 0x00u, 0x0Au, 0x10u,
                                      0x70u, '\\',  'T', 'R', 'A', 'C', '\\', 'S',   'N',   'A',   'P'};
    static const uint8_t creates_own[] = {0x5Bu, 0x80u, 'D', 'Y', 'N', 'L', 0x02u, 0x00u, 0x0Au, 0x10u};
    static const uint8_t creates_memory[] = {0x5Bu, 0x80u, '^',   'D',   'Y',   'N',  'M',
                                             0x00u, 0x0Bu, 0x00u, 0x30u, 0x0Au, 0x10u};
    static const uint8_t creates_other[] = {0x5Bu, 0x80u, '^',   'D',   'Y',  'N', 'R', 0x02u, 0x00u,
                                            0x0Au, 0x10u, 0x70u, 0x01u, '\\', 'S', 'N', 'A',   'P'};
    static const uint8_t traces[] = {0x70u, 0x72u, 0x77u, '\\',  'T',  'R', 'A', 'C', 0x0Au, 0x10u,
                                     0x00u, 0x0Au, 0x0Fu, 0x00u, '\\', 'T', 'R', 'A', 'C'};

    initialization_begin();
    made_up_aml_name_integer(&initialization_aml, "SNAP", 0u);
    made_up_aml_begin(&initialization_inner);
    initialization_registration(&initialization_inner);
    initialization_method(&initialization_inner, "MKRG", 0x00u, creates, sizeof(creates));
    initialization_method(&initialization_inner, "MKRL", 0x00u, creates_own, sizeof(creates_own));
    initialization_method(&initialization_inner, "MKMM", 0x00u, creates_memory, sizeof(creates_memory));
    initialization_device(&initialization_aml, "XDEV", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_REG", 0x01u, traces, sizeof(traces));
    initialization_method(&initialization_inner, "MKRG", 0x00u, creates_other, sizeof(creates_other));
    initialization_device(&initialization_aml, "YDEV", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_failing(&initialization_inner, "_REG", 0x02u, false);
    initialization_method(&initialization_inner, "MKRG", 0x00u, creates_other, sizeof(creates_other));
    initialization_device(&initialization_aml, "WDEV", 0u, &initialization_inner);
    if (!initialization_run(test, false))
        return;
    kernel_test_check(
        test, initialization_call("\\XDEV.MKRG") == INITIALIZATION_STATUS(OK) && initialization_integer("TRAC") == 0u,
        "a region created before the initialisation gets no _REG");
    advanced_configuration_and_power_interface_initialize_namespace(&initialization_load, &initialization_machine,
                                                                    &initialization_counts);
    kernel_test_check(test, initialization_integer("TRAC") == 0u && initialization_counts.registrations == 0u,
                      "the initialisation registers nothing: no table declares a PCI_Config region");

    const uint32_t implicit_returns = initialization_load.evaluation.implicit_returns;

    kernel_test_check(test,
                      initialization_call("\\XDEV.MKRG") == INITIALIZATION_STATUS(OK) &&
                          initialization_integer("TRAC") == 0x21u && initialization_integer("SNAP") == 0x21u,
                      "XDEV's _REG (2, 1) runs at once, before the statement after the OperationRegion");
    kernel_test_check(test, initialization_load.evaluation.implicit_returns == implicit_returns + 1u,
                      "MKRG's implicit return to the host is counted, not _REG's, whose result nothing uses");
    kernel_test_check(test,
                      initialization_call("\\XDEV.MKRL") == INITIALIZATION_STATUS(OK) &&
                          initialization_call("\\XDEV.MKMM") == INITIALIZATION_STATUS(OK) &&
                          initialization_integer("TRAC") == 0x21u,
                      "a region in the method's own scope, or of SystemMemory, gets no _REG");
    kernel_test_check(test,
                      initialization_call("\\YDEV.MKRG") == INITIALIZATION_STATUS(OK) &&
                          initialization_integer("SNAP") == 1u && initialization_integer("TRAC") == 0x21u,
                      "a _REG of one argument is not called, and the method goes on");
    kernel_test_check(test, initialization_load.regions.pci_registration_failures == 1u,
                      "the _REG not called is counted as failed");
    kernel_test_check(test,
                      initialization_call("\\WDEV.MKRG") == INITIALIZATION_STATUS(OK) &&
                          initialization_integer("SNAP") == 1u &&
                          initialization_load.regions.pci_registration_failures == 2u,
                      "a _REG that fails is dropped, counted, and the method that created the region goes on");
    kernel_test_check(test,
                      initialization_load.regions.pci_registrations == 2u &&
                          initialization_load.regions.internal_evaluations == 0u &&
                          initialization_load.regions.internal_failures == 0u,
                      "two _REG ran at a region's creation, XDEV's and WDEV's, counted apart from the attaches");
    initialization_close(test);
}

/**
 * @brief A _REG that creates a PCI_Config region in its own scope is not called again for it, whether
 *        a region's creation or AML called it: the method that called it still succeeds, and the trace
 *        holds one _REG per call.
 */
KERNEL_TEST(a_registration_that_creates_a_region_in_its_scope_runs_once)
{
    static const uint8_t registers[] = {0x70u, 0x72u, 0x77u, 0x72u, 0x77u, '\\',  'T',   'R',   'A',
                                        'C',   0x0Au, 0x10u, 0x00u, 0x68u, 0x00u, 0x0Au, 0x10u, 0x00u,
                                        0x69u, 0x00u, '\\',  'T',   'R',   'A',   'C',   0x5Bu, 0x80u,
                                        '^',   'D',   'Y',   'N',   'Z',   0x02u, 0x00u, 0x0Au, 0x10u};
    static const uint8_t creates[] = {0x5Bu, 0x80u, '^', 'D', 'Y', 'N', 'R', 0x02u, 0x00u, 0x0Au, 0x10u};
    static const uint8_t calls[] = {'_', 'R', 'E', 'G', 0x0Au, 0x02u, 0x01u};

    initialization_begin();
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_REG", 0x02u, registers, sizeof(registers));
    initialization_method(&initialization_inner, "MKRG", 0x00u, creates, sizeof(creates));
    initialization_method(&initialization_inner, "CALR", 0x00u, calls, sizeof(calls));
    initialization_device(&initialization_aml, "ZDEV", 0u, &initialization_inner);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_call("\\ZDEV.MKRG") == INITIALIZATION_STATUS(OK),
                      "the method that creates the first region succeeds");
    kernel_test_check(test, initialization_integer("TRAC") == 0x21u, "_REG runs once");
    kernel_test_check(test,
                      initialization_call("\\ZDEV.CALR") == INITIALIZATION_STATUS(OK) &&
                          initialization_integer("TRAC") == 0x2121u,
                      "a _REG AML calls itself, which creates such a region, runs once too");
    kernel_test_check(test, initialization_load.regions.pci_registration_failures == 0u, "no _REG failed");
    initialization_close(test);
}

/**
 * @brief \_PIC is not called while the kernel routes PCI interrupts through the 8259, and its
 *        deferral is counted; without a \_PIC nothing is deferred (§3.8 step 5).
 */
KERNEL_TEST(the_interrupt_model_method_is_not_called)
{
    static const uint8_t stores[] = {0x70u, 0x68u, '\\', 'P', 'I', 'C', 'M'};

    initialization_begin();
    made_up_aml_name_integer(&initialization_aml, "PICM", 0xFFu);
    initialization_method(&initialization_aml, "_PIC", 0x01u, stores, sizeof(stores));
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("PICM") == 0xFFu, "\\_PIC never runs");
    kernel_test_check(test, initialization_counts.pic_mode_deferred == 1u, "its deferral is counted");
    initialization_close(test);

    initialization_begin();
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_counts.pic_mode_deferred == 0u, "without \\_PIC nothing is deferred");
    initialization_close(test);
}

/**
 * @brief The Notifies the sequence raises are dropped once it ends (§3.8 step 9): none waits in the
 *        ring for a consumer that does not exist yet.
 */
KERNEL_TEST(the_notifies_the_sequence_raises_are_dropped_at_its_end)
{
    static const uint8_t notifies[] = {0x86u, '\\', 'N', 'O', 'T', 'E', 0x0Au, 0x80u};

    initialization_begin();
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_INI", 0x00u, notifies, sizeof(notifies));
    initialization_device(&initialization_aml, "NOTE", 0u, &initialization_inner);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test,
                      initialization_load.services.notifications == 1u &&
                          initialization_load.services.notification_count == 0u &&
                          initialization_counts.initialization_failed == 0u,
                      "NOTE's _INI raised one Notify, and the ring is empty");
    initialization_close(test);
}

/**
 * @brief A device whose subtree holds no _INI, or only an External one, has its _STA left
 *        unevaluated; one with an _INI two levels down has its _STA evaluated; an _INI that is an
 *        Alias runs what it names (§3.8 step 4, O14).
 */
KERNEL_TEST(a_subtree_without_initialization_evaluates_no_status)
{
    static const uint8_t counts[] = {0x75u, '\\', 'S', 'T', 'A', 'C', 0xA4u, 0x0Au, 0x0Fu};
    static const uint8_t external[] = {0x15u, '_', 'I', 'N', 'I', 0x08u, 0x00u};
    static const uint8_t alias[] = {0x06u, '\\', 'I', 'N', 'I', 'M', '_', 'I', 'N', 'I'};

    initialization_begin();
    made_up_aml_name_integer(&initialization_aml, "STAC", 0u);
    made_up_aml_begin(&initialization_inner);
    initialization_device(&initialization_inner, "CHLD", 0u, NULL);
    made_up_aml_begin(&initialization_outer);
    initialization_method(&initialization_outer, "_STA", 0x00u, counts, sizeof(counts));
    made_up_aml_raw(&initialization_outer, initialization_inner.bytes, initialization_inner.length);
    made_up_aml_device(&initialization_aml, "DNOI", &initialization_outer);
    made_up_aml_begin(&initialization_inner);
    initialization_device(&initialization_inner, "LEAF", 1u, NULL);
    made_up_aml_begin(&initialization_outer);
    initialization_device(&initialization_outer, "MID_", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_STA", 0x00u, counts, sizeof(counts));
    made_up_aml_raw(&initialization_inner, initialization_outer.bytes, initialization_outer.length);
    made_up_aml_device(&initialization_aml, "DDEP", &initialization_inner);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_raw(&initialization_outer, external, sizeof(external));
    initialization_method(&initialization_outer, "_STA", 0x00u, counts, sizeof(counts));
    made_up_aml_device(&initialization_aml, "DEXT", &initialization_outer);
    made_up_aml_begin(&initialization_method_body);
    initialization_trace(&initialization_method_body, 2u);
    made_up_aml_method(&initialization_aml, "INIM", 0x00u, &initialization_method_body);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_raw(&initialization_outer, alias, sizeof(alias));
    made_up_aml_device(&initialization_aml, "DALI", &initialization_outer);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("STAC") == 1u && initialization_counts.status_evaluations == 1u,
                      "only DDEP's _STA is evaluated: an External _INI holds no _INI");
    kernel_test_check(test, initialization_integer("TRAC") == 0x12u,
                      "LEAF's _INI runs (1), then DALI's, an Alias of \\INIM (2)");
    kernel_test_check(test, initialization_counts.devices == 9u,
                      "nine devices in the namespace, \\_SB_ and \\_TZ_ included");
    initialization_close(test);
}

/**
 * @brief Each evaluation the kernel asks for starts its budgets of §6.1 from zero: whatever the last
 *        table load left of its While back-edges and field accesses does not count against a method
 *        that reads a field once and takes one back-edge.
 */
KERNEL_TEST(each_evaluation_starts_its_budgets_from_zero)
{
    static const MadeUpFieldUnit_t unit[] = {
        {"FLD0", 8u}
    };
    static const uint8_t reads[] = {0x70u, 'F',   'L',   'D',   '0',   0x60u, 0x70u, 0x00u, 0x61u,
                                    0xA2u, 0x06u, 0x95u, 0x61u, 0x01u, 0x75u, 0x61u, 0xA4u, 0x60u};

    initialization_begin();
    made_up_aml_region(&initialization_aml, "MEM0", INITIALIZATION_SYSTEM_MEMORY, 0x1000u, 4u);
    made_up_aml_field(&initialization_aml, "MEM0", 0x01u, unit, 1u);
    initialization_method(&initialization_aml, "READ", 0x00u, reads, sizeof(reads));
    if (!initialization_run(test, false))
        return;
    initialization_load.evaluation.field_accesses =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FIELD_ACCESSES;
    initialization_load.evaluation.iterations =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_LOOP_ITERATIONS;
    kernel_test_check(test, initialization_call("\\READ") == INITIALIZATION_STATUS(OK),
                      "a method that reads a field and loops once succeeds after a load that spent both budgets");
    initialization_close(test);
}

/**
 * @brief \_SB._OSC is called with the platform UUID, revision 1 and two DWORDs, first as a query then as
 *        a commit, and claims no DWORD 2 bit in either (§3.8 step 6).
 */
KERNEL_TEST(the_platform_is_asked_for_nothing_with_a_query_then_a_commit)
{
    static MadeUpAml_t system_bus;
    uint8_t uuid[16];

    initialization_begin();
    initialization_capabilities_names(&initialization_aml, 'P');
    made_up_aml_begin(&system_bus);
    initialization_capabilities_recorder(&system_bus, 'P', 1u, 0u);
    made_up_aml_scope(&initialization_aml, "_SB_", &system_bus);
    if (!initialization_run(test, true))
        return;
    initialization_uuid("0811B06E-4A27-44F9-8D60-3CBBC22E7B48", uuid);
    kernel_test_check(test, initialization_integer("TRAC") == 0x21u, "a query (2), then a commit (1)");
    kernel_test_check(test, initialization_integer("PCAP") == 0u, "neither call claims a DWORD 2 bit");
    kernel_test_check(test,
                      initialization_buffer_holds("PUID", uuid, sizeof(uuid)) && initialization_integer("PREV") == 1u &&
                          initialization_integer("PCNT") == 2u,
                      "the platform UUID, revision 1, two DWORDs");
    kernel_test_check(test,
                      initialization_counts.platform_capabilities ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_GRANTED,
                      "granted");
    initialization_close(test);
}

/**
 * @brief A query answered with an error bit is not committed, and is counted refused; an _OSC that fails,
 *        or returns no Buffer, is counted failed and named; a \_SB without _OSC is absent (§3.8, the
 *        failure table).
 */
KERNEL_TEST(the_platform_answers_absent_refused_or_failed_are_told_apart)
{
    static MadeUpAml_t system_bus;
    static const uint8_t returns_one[] = {0xA4u, 0x01u};

    initialization_begin();
    initialization_capabilities_names(&initialization_aml, 'P');
    made_up_aml_begin(&system_bus);
    initialization_capabilities_recorder(&system_bus, 'P', 1u, 0x02u);
    made_up_aml_scope(&initialization_aml, "_SB_", &system_bus);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test,
                      initialization_integer("TRAC") == 0x2u &&
                          initialization_counts.platform_capabilities ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_REFUSED &&
                          initialization_counts.first_failure_node == 0u,
                      "an error bit in the query's DWORD 1: no commit, refused, no failure named");
    initialization_close(test);

    initialization_begin();
    made_up_aml_begin(&system_bus);
    initialization_failing(&system_bus, "_OSC", 0x04u, true);
    made_up_aml_scope(&initialization_aml, "_SB_", &system_bus);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test,
                      initialization_counts.platform_capabilities ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_FAILED &&
                          initialization_counts.first_failure == INITIALIZATION_STATUS(DIVIDE_BY_ZERO),
                      "an _OSC that divides by zero fails, named");
    initialization_close(test);

    initialization_begin();
    made_up_aml_begin(&system_bus);
    initialization_method(&system_bus, "_OSC", 0x04u, returns_one, sizeof(returns_one));
    made_up_aml_scope(&initialization_aml, "_SB_", &system_bus);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test,
                      initialization_counts.platform_capabilities ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_FAILED &&
                          initialization_counts.first_failure == INITIALIZATION_STATUS(TYPE_MISMATCH),
                      "an _OSC that returns an Integer fails as a type mismatch");
    initialization_close(test);

    initialization_begin();
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test,
                      initialization_counts.platform_capabilities ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_ABSENT,
                      "no _OSC: absent");
    initialization_close(test);
}

/**
 * @brief A processor object bound to a CPU slot gets the C-state bits, 3 and 4, through its _OSC with
 *        Intel's UUID, a commit, or its _PDC with a buffer of revision 1 and one DWORD when it has no _OSC;
 *        one bound to no slot, and a Device that is no processor, get nothing (§3.8 step 7, §3.10).
 */
KERNEL_TEST(every_bound_processor_is_given_its_c_states_through_osc_or_pdc)
{
    static const uint32_t identifiers[] = {1u, 2u};
    static const uint8_t driver_capabilities[] = {1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0x18u, 0u, 0u, 0u};
    static const uint8_t unique_identifier[] = {0x08u, '_', 'U', 'I', 'D', 0x0Au, 0x02u};
    static const uint8_t bridge_identifier[] = {0x08u, '_', 'H', 'I', 'D', 0x0Cu, 0x41u, 0xD0u, 0x0Au, 0x08u};
    uint8_t uuid[16];

    initialization_begin();
    initialization_slots(identifiers, 2u);
    initialization_capabilities_names(&initialization_aml, 'A');
    initialization_capabilities_names(&initialization_aml, 'C');
    initialization_capabilities_names(&initialization_aml, 'N');
    made_up_aml_name_integer(&initialization_aml, "XPDC", 0u);
    made_up_aml_name_integer(&initialization_aml, "BPDC", 0u);
    made_up_aml_begin(&initialization_inner);
    initialization_capabilities_recorder(&initialization_inner, 'A', 3u, 0u);
    initialization_driver_capabilities_recorder(&initialization_inner, 'X', 9u);
    made_up_aml_processor(&initialization_aml, "CPU0", 1u, 0x410u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_hardware_identifier(&initialization_inner, "ACPI0007");
    made_up_aml_raw(&initialization_inner, unique_identifier, sizeof(unique_identifier));
    initialization_driver_capabilities_recorder(&initialization_inner, 'B', 4u);
    made_up_aml_device(&initialization_aml, "CPU1", &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_capabilities_recorder(&initialization_inner, 'N', 5u, 0u);
    made_up_aml_processor(&initialization_aml, "CPU9", 9u, 0x410u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_raw(&initialization_inner, bridge_identifier, sizeof(bridge_identifier));
    initialization_capabilities_recorder(&initialization_inner, 'C', 6u, 0u);
    made_up_aml_device(&initialization_aml, "PCI0", &initialization_inner);
    if (!initialization_run(test, true))
        return;
    initialization_uuid("4077A616-290C-47BE-9EBD-D87058713953", uuid);
    kernel_test_check(test, initialization_integer("TRAC") == 0x34u,
                      "CPU0's _OSC (3), not its _PDC, then CPU1's _PDC (4); neither CPU9 nor PCI0");
    kernel_test_check(test,
                      initialization_buffer_holds("AUID", uuid, sizeof(uuid)) && initialization_integer("AREV") == 1u &&
                          initialization_integer("ACNT") == 2u && initialization_integer("ACAP") == 0x18u,
                      "Intel's UUID, revision 1, two DWORDs, bits 3 and 4");
    kernel_test_check(test, initialization_buffer_holds("BPDC", driver_capabilities, sizeof(driver_capabilities)),
                      "_PDC's buffer: revision 1, one DWORD, bits 3 and 4");
    kernel_test_check(test,
                      initialization_counts.processor_capabilities == 2u &&
                          initialization_counts.processor_capabilities_legacy == 1u &&
                          initialization_counts.processors_unbound == 1u &&
                          initialization_counts.processor_capabilities_refused == 0u &&
                          initialization_counts.processor_capabilities_failed == 0u,
                      "two processors given the capabilities, one through _PDC, one unbound");
    initialization_close(test);
}

/**
 * @brief On processors that implement MONITOR/MWAIT the capabilities add bits 8 and 9, so that _CST
 *        names MWAIT hints; without a MADT no processor binds (§3.8 step 7).
 */
KERNEL_TEST(monitor_wait_adds_the_mwait_bits_and_no_madt_binds_nothing)
{
    static const uint32_t identifiers[] = {1u};

    initialization_begin();
    initialization_slots(identifiers, 1u);
    initialization_machine.monitor_wait = true;
    initialization_capabilities_names(&initialization_aml, 'A');
    made_up_aml_begin(&initialization_inner);
    initialization_capabilities_recorder(&initialization_inner, 'A', 3u, 0u);
    made_up_aml_processor(&initialization_aml, "CPU0", 1u, 0x410u, 6u, &initialization_inner);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("ACAP") == 0x318u, "bits 3, 4, 8 and 9");
    initialization_close(test);

    initialization_begin();
    initialization_machine.madt = NULL;
    initialization_capabilities_names(&initialization_aml, 'A');
    made_up_aml_begin(&initialization_inner);
    initialization_capabilities_recorder(&initialization_inner, 'A', 3u, 0u);
    made_up_aml_processor(&initialization_aml, "CPU0", 1u, 0x410u, 6u, &initialization_inner);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test,
                      initialization_integer("TRAC") == 0u && initialization_counts.processors_unbound == 1u &&
                          initialization_counts.processor_capabilities == 0u,
                      "no MADT: the processor is unbound and given nothing");
    initialization_close(test);
}

/**
 * @brief A processor whose _OSC returns an error bit is counted refused, one whose _OSC fails is counted
 *        failed and named, and the processors after them still get theirs (§3.8, the failure table).
 */
KERNEL_TEST(a_processor_that_refuses_or_fails_is_counted_and_the_next_one_goes_on)
{
    static const uint32_t identifiers[] = {1u, 2u, 3u};

    initialization_begin();
    initialization_slots(identifiers, 3u);
    initialization_capabilities_names(&initialization_aml, 'A');
    made_up_aml_name_integer(&initialization_aml, "CPDC", 0u);
    made_up_aml_begin(&initialization_inner);
    initialization_capabilities_recorder(&initialization_inner, 'A', 3u, 0x02u);
    made_up_aml_processor(&initialization_aml, "CPU0", 1u, 0x410u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_failing(&initialization_inner, "_OSC", 0x04u, true);
    made_up_aml_processor(&initialization_aml, "CPU1", 2u, 0x410u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_driver_capabilities_recorder(&initialization_inner, 'C', 5u);
    made_up_aml_processor(&initialization_aml, "CPU2", 3u, 0x410u, 6u, &initialization_inner);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test, initialization_integer("TRAC") == 0x35u, "CPU0's _OSC (3), then CPU2's _PDC (5)");
    kernel_test_check(test,
                      initialization_counts.processor_capabilities == 3u &&
                          initialization_counts.processor_capabilities_refused == 1u &&
                          initialization_counts.processor_capabilities_failed == 1u &&
                          initialization_counts.first_failure == INITIALIZATION_STATUS(DIVIDE_BY_ZERO),
                      "three processors: one refused, one failed and named");
    initialization_close(test);
}
