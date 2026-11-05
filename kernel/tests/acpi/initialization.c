#include "fixtures/initialization.h"
#include "fixtures/common.h"
#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/machine_language.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_initialization, KERNEL_TEST_STAGE_INITIALIZATION);

#define INITIALIZATION_PCI_CONFIG    0x02u /**< The RegionSpace byte of PCI_Config. */
#define INITIALIZATION_SYSTEM_MEMORY 0x00u /**< The RegionSpace byte of SystemMemory. */
#define INITIALIZATION_SYSTEM_IO     0x01u /**< The RegionSpace byte of SystemIO. */
#define INITIALIZATION_STATUS(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define INITIALIZATION_ROUTER_INDEX  0x0C00u     /**< The index port of AMD's PCI interrupt router. */
#define INITIALIZATION_ROUTER_DATA   0x0C01u     /**< Its data port. */
#define INITIALIZATION_NVS_ADDRESS   0x00320000u /**< Where GNVS, the NVS region of Intel's HWP pattern, lies. */
#define INITIALIZATION_HWP_ADDRESS   0x00330000u /**< Where the HWP table that pattern loads lies. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t initialization_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t initialization_load;
static AdvancedConfigurationAndPowerInterfaceInitialization_t initialization_counts;
static AdvancedConfigurationAndPowerInterfaceMadtState_t initialization_madt;
static AdvancedConfigurationAndPowerInterfaceInitializationMachine_t initialization_machine;
static AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t initialization_idle;
static uint8_t initialization_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static uint8_t initialization_hwp_table[MADE_UP_TABLE_HEADER + sizeof(initialization_hwp_table_aml)];
static uint32_t initialization_nvs[3];
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
                                                                        &initialization_counts, &initialization_idle);
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
 *
 * Its pieces are raw bytes: the Store and the Add it opens around @p digit, partial terms.
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
 *
 * Its Return opcode is raw bytes: a partial term around @p status.
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
    if (returns)
        initialization_method(aml, name, arguments, common_return_divide_by_zero_aml,
                              sizeof(common_return_divide_by_zero_aml));
    else
        initialization_method(aml, name, arguments, common_divide_by_zero_aml, sizeof(common_divide_by_zero_aml));
}

/**
 * @brief Appends Method (_REG, 2), which traces its two arguments, Arg0 then Arg1.
 */
static void initialization_registration(MadeUpAml_t *aml)
{
    initialization_method(aml, "_REG", 0x02u, initialization_registration_traces_aml,
                          sizeof(initialization_registration_traces_aml));
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
 *
 * Its whole terms come from fixtures; its other pieces are raw bytes: partial terms around the names of @p record,
 * @p digit and @p error_bits.
 */
static void initialization_capabilities_recorder(MadeUpAml_t *aml, char record, uint8_t digit, uint8_t error_bits)
{
    static const uint8_t trace_head[] = {0x70u, 0x72u, 0x77u, '\\', 'T', 'R', 'A', 'C',  0x0Au,
                                         0x10u, 0x00u, 0x72u, 'C',  'D', 'W', '1', 0x0Au};
    static const uint8_t trace_tail[] = {0x00u, 0x00u, '\\', 'T', 'R', 'A', 'C'};
    static const uint8_t or_byte = 0x7Du;
    static const uint8_t refuse_head[] = {0x7Du, 'C', 'D', 'W', '1', 0x0Au};
    static const uint8_t refuse_tail[] = {'C', 'D', 'W', '1'};
    static const char *const kept[] = {"UID", "REV", "CNT"};

    made_up_aml_begin(&initialization_method_body);
    for (uint8_t argument = 0u; argument < 3u; ++argument)
    {
        const uint8_t copy[] = {0x9Du, (uint8_t) (0x68u + argument)};

        made_up_aml_raw(&initialization_method_body, copy, sizeof(copy));
        initialization_root_name(&initialization_method_body, record, kept[argument]);
    }
    made_up_aml_raw(&initialization_method_body, initialization_first_dword_field_aml,
                    sizeof(initialization_first_dword_field_aml));
    made_up_aml_raw(&initialization_method_body, initialization_second_dword_field_aml,
                    sizeof(initialization_second_dword_field_aml));
    made_up_aml_raw(&initialization_method_body, trace_head, sizeof(trace_head));
    made_up_aml_raw(&initialization_method_body, &digit, 1u);
    made_up_aml_raw(&initialization_method_body, trace_tail, sizeof(trace_tail));
    made_up_aml_raw(&initialization_method_body, &or_byte, 1u);
    initialization_root_name(&initialization_method_body, record, "CAP");
    made_up_aml_raw(&initialization_method_body, (const uint8_t *) "CDW2", 4u);
    initialization_root_name(&initialization_method_body, record, "CAP");
    if (error_bits != 0u)
    {
        made_up_aml_raw(&initialization_method_body, refuse_head, sizeof(refuse_head));
        made_up_aml_raw(&initialization_method_body, &error_bits, 1u);
        made_up_aml_raw(&initialization_method_body, refuse_tail, sizeof(refuse_tail));
    }
    made_up_aml_raw(&initialization_method_body, initialization_return_arg3_aml,
                    sizeof(initialization_return_arg3_aml));
    made_up_aml_method(aml, "_OSC", 0x04u, &initialization_method_body);
}

/**
 * @brief Appends Method (_PDC, 1) { CopyObject (Arg0, \@p record PDC); trace @p digit }.
 *
 * Its CopyObject is raw bytes: a partial term around the name of @p record.
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
 *
 * Its Name is raw bytes: a partial term around @p identifier.
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
 *
 * Its written_at array is a UUID's byte order, not AML.
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
 * @brief How many writes to the I/O port @p port the simulated host logged, of @p value, or of any value when
 *        @p value is above 0xFF.
 */
static uint32_t initialization_port_writes(uint16_t port, uint32_t value)
{
    const uint32_t logged =
        initialization_host.access_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES ?
            initialization_host.access_count :
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;
    uint32_t writes = 0u;

    for (uint32_t index = 0u; index < logged; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t *access = &initialization_host.accesses[index];

        if (access->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_WRITE && access->port == port &&
            (value > 0xFFu || access->value == value))
            ++writes;
    }
    return writes;
}

/**
 * @brief Evaluates \_PIC with the Integer @p model, and gives back its status.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t initialization_interrupt_model(uint64_t model)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t argument = {
        .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
        .payload = model};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&initialization_load, "\\_PIC", &argument,
                                                                             1u, &result);

    advanced_configuration_and_power_interface_machine_language_value_release(&initialization_load.heap,
                                                                              &initialization_load.space, &result);
    return status;
}

/**
 * @brief Loads Intel's HWP pattern on a machine whose one CPU slot binds CPU0, its GNVS telling where the HWP table
 *        lies and the platform granting @p granted unasked, then runs the initialisation.
 */
static bool initialization_run_hardware_p_states(KernelTest_t *test, uint32_t granted)
{
    static const uint32_t identifiers[] = {1u};

    initialization_begin();
    initialization_slots(identifiers, 1u);
    made_up_definition_block(initialization_hwp_table, "SSDT", 2u, initialization_hwp_table_aml,
                             sizeof(initialization_hwp_table_aml));
    initialization_nvs[0] = INITIALIZATION_HWP_ADDRESS;
    initialization_nvs[1] = sizeof(initialization_hwp_table);
    initialization_nvs[2] = granted;
    advanced_configuration_and_power_interface_simulated_host_place(&initialization_host, INITIALIZATION_NVS_ADDRESS,
                                                                    initialization_nvs, sizeof(initialization_nvs));
    advanced_configuration_and_power_interface_simulated_host_place(
        &initialization_host, INITIALIZATION_HWP_ADDRESS, initialization_hwp_table, sizeof(initialization_hwp_table));
    made_up_aml_raw(&initialization_aml, initialization_intel_hardware_p_states_aml,
                    sizeof(initialization_intel_hardware_p_states_aml));
    return initialization_run(test, true);
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
    made_up_aml_raw(&initialization_outer, initialization_package_status_aml,
                    sizeof(initialization_package_status_aml));
    initialization_ini(&initialization_outer, 4u);
    initialization_device(&initialization_aml, "DPKG", 0u, &initialization_outer);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_raw(&initialization_outer, initialization_empty_status_aml, sizeof(initialization_empty_status_aml));
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
    initialization_begin();
    made_up_aml_name_integer(&initialization_aml, "SNAP", 0u);
    made_up_aml_begin(&initialization_inner);
    initialization_registration(&initialization_inner);
    initialization_method(&initialization_inner, "MKRG", 0x00u, initialization_creates_parent_region_aml,
                          sizeof(initialization_creates_parent_region_aml));
    initialization_method(&initialization_inner, "MKRL", 0x00u, initialization_creates_own_region_aml,
                          sizeof(initialization_creates_own_region_aml));
    initialization_method(&initialization_inner, "MKMM", 0x00u, initialization_creates_parent_memory_region_aml,
                          sizeof(initialization_creates_parent_memory_region_aml));
    initialization_device(&initialization_aml, "XDEV", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_REG", 0x01u, initialization_trace_f_aml,
                          sizeof(initialization_trace_f_aml));
    initialization_method(&initialization_inner, "MKRG", 0x00u, initialization_creates_parent_region_then_marks_aml,
                          sizeof(initialization_creates_parent_region_then_marks_aml));
    initialization_device(&initialization_aml, "YDEV", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_failing(&initialization_inner, "_REG", 0x02u, false);
    initialization_method(&initialization_inner, "MKRG", 0x00u, initialization_creates_parent_region_then_marks_aml,
                          sizeof(initialization_creates_parent_region_then_marks_aml));
    initialization_device(&initialization_aml, "WDEV", 0u, &initialization_inner);
    if (!initialization_run(test, false))
        return;
    kernel_test_check(
        test, initialization_call("\\XDEV.MKRG") == INITIALIZATION_STATUS(OK) && initialization_integer("TRAC") == 0u,
        "a region created before the initialisation gets no _REG");
    advanced_configuration_and_power_interface_initialize_namespace(&initialization_load, &initialization_machine,
                                                                    &initialization_counts, &initialization_idle);
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
 * @brief A _REG whose SyncLevel keeps it from running at a region's creation, refused with mutex_order, takes back
 *        the two arguments it was given: the method that created the region still has the whole value stack, as it
 *        has before the initialisation, when no _REG is called, which a VarPackage and its count fill (§5.3, §6.1).
 *
 * The creator's Return opcode is raw bytes, a partial term around the VarPackage the test builds, and so are that
 * VarPackage's opcode and its One initialisers: the test names their count.
 */
KERNEL_TEST(a_registration_refused_at_a_region_creation_takes_its_arguments_back)
{
    static const uint8_t return_byte = 0xA4u;
    static const uint8_t variable_package = 0x13u;
    static const uint8_t one = 0x01u;
    static MadeUpAml_t elements;
    static MadeUpAml_t creator;
    const uint32_t initialisers = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES - 1u;
    const uint8_t count[] = {0x0Bu, (uint8_t) initialisers, (uint8_t) (initialisers >> 8u)};

    initialization_begin();
    made_up_aml_raw(&initialization_aml, initialization_mutex_aml, sizeof(initialization_mutex_aml));
    made_up_aml_begin(&elements);
    made_up_aml_raw(&elements, count, sizeof(count));
    for (uint32_t element = 0u; element < initialisers; ++element)
        made_up_aml_raw(&elements, &one, 1u);
    made_up_aml_begin(&creator);
    made_up_aml_raw(&creator, initialization_creates_region_under_mutex_aml,
                    sizeof(initialization_creates_region_under_mutex_aml));
    made_up_aml_raw(&creator, &return_byte, 1u);
    made_up_aml_construct(&creator, &variable_package, 1u, &elements);
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_REG", 0x0Au, initialization_trace_one_aml,
                          sizeof(initialization_trace_one_aml));
    made_up_aml_method(&initialization_inner, "MKRG", 0x00u, &creator);
    initialization_device(&initialization_aml, "RDEV", 0u, &initialization_inner);
    if (!kernel_test_check(test, !elements.overflowed && !creator.overflowed, "the VarPackage fits") ||
        !initialization_run(test, false))
        return;
    kernel_test_check(test,
                      initialization_call("\\RDEV.MKRG") == INITIALIZATION_STATUS(OK) &&
                          initialization_load.evaluation.values_peak ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES,
                      "before the initialisation no _REG is called, and the VarPackage and its count fill the stack");
    advanced_configuration_and_power_interface_initialize_namespace(&initialization_load, &initialization_machine,
                                                                    &initialization_counts, &initialization_idle);
    kernel_test_check(test, initialization_call("\\RDEV.MKRG") == INITIALIZATION_STATUS(OK),
                      "after it, the method whose region's _REG was refused still has the whole stack");
    kernel_test_check(test,
                      initialization_integer("TRAC") == 0u && initialization_load.regions.pci_registrations == 1u &&
                          initialization_load.regions.pci_registration_failures == 1u,
                      "_REG did not run: its call was refused, and counted as failed");
    initialization_close(test);
}

/**
 * @brief A _REG that creates a PCI_Config region in its own scope is not called again for it, whether
 *        a region's creation or AML called it: the method that called it still succeeds, and the trace
 *        holds one _REG per call.
 */
KERNEL_TEST(a_registration_that_creates_a_region_in_its_scope_runs_once)
{
    initialization_begin();
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_REG", 0x02u, initialization_registration_creates_region_aml,
                          sizeof(initialization_registration_creates_region_aml));
    initialization_method(&initialization_inner, "MKRG", 0x00u, initialization_creates_region_above_aml,
                          sizeof(initialization_creates_region_above_aml));
    initialization_method(&initialization_inner, "CALR", 0x00u, initialization_calls_registration_aml,
                          sizeof(initialization_calls_registration_aml));
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
    initialization_begin();
    made_up_aml_name_integer(&initialization_aml, "PICM", 0xFFu);
    initialization_method(&initialization_aml, "_PIC", 0x01u, initialization_stores_interrupt_model_aml,
                          sizeof(initialization_stores_interrupt_model_aml));
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
    initialization_begin();
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_INI", 0x00u, initialization_notifies_aml,
                          sizeof(initialization_notifies_aml));
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
 *
 * Its External is raw bytes: iasl emits an External at the root only inside an If (Zero).
 */
KERNEL_TEST(a_subtree_without_initialization_evaluates_no_status)
{
    static const uint8_t external[] = {0x15u, '_', 'I', 'N', 'I', 0x08u, 0x00u};

    initialization_begin();
    made_up_aml_name_integer(&initialization_aml, "STAC", 0u);
    made_up_aml_begin(&initialization_inner);
    initialization_device(&initialization_inner, "CHLD", 0u, NULL);
    made_up_aml_begin(&initialization_outer);
    initialization_method(&initialization_outer, "_STA", 0x00u, initialization_counted_status_aml,
                          sizeof(initialization_counted_status_aml));
    made_up_aml_raw(&initialization_outer, initialization_inner.bytes, initialization_inner.length);
    made_up_aml_device(&initialization_aml, "DNOI", &initialization_outer);
    made_up_aml_begin(&initialization_inner);
    initialization_device(&initialization_inner, "LEAF", 1u, NULL);
    made_up_aml_begin(&initialization_outer);
    initialization_device(&initialization_outer, "MID_", 0u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_STA", 0x00u, initialization_counted_status_aml,
                          sizeof(initialization_counted_status_aml));
    made_up_aml_raw(&initialization_inner, initialization_outer.bytes, initialization_outer.length);
    made_up_aml_device(&initialization_aml, "DDEP", &initialization_inner);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_raw(&initialization_outer, external, sizeof(external));
    initialization_method(&initialization_outer, "_STA", 0x00u, initialization_counted_status_aml,
                          sizeof(initialization_counted_status_aml));
    made_up_aml_device(&initialization_aml, "DEXT", &initialization_outer);
    made_up_aml_begin(&initialization_method_body);
    initialization_trace(&initialization_method_body, 2u);
    made_up_aml_method(&initialization_aml, "INIM", 0x00u, &initialization_method_body);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_raw(&initialization_outer, initialization_initialization_alias_aml,
                    sizeof(initialization_initialization_alias_aml));
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

    initialization_begin();
    made_up_aml_region(&initialization_aml, "MEM0", INITIALIZATION_SYSTEM_MEMORY, 0x1000u, 4u);
    made_up_aml_field(&initialization_aml, "MEM0", 0x01u, unit, 1u);
    initialization_method(&initialization_aml, "READ", 0x00u, initialization_reads_field_aml,
                          sizeof(initialization_reads_field_aml));
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
 *        or returns no Buffer of two DWORDs, is counted failed and named; a \_SB without _OSC is absent
 *        (§3.8, the failure table).
 */
KERNEL_TEST(the_platform_answers_absent_refused_or_failed_are_told_apart)
{
    static MadeUpAml_t system_bus;

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
    initialization_method(&system_bus, "_OSC", 0x04u, initialization_return_one_aml,
                          sizeof(initialization_return_one_aml));
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
    made_up_aml_begin(&system_bus);
    initialization_method(&system_bus, "_OSC", 0x04u, initialization_return_short_buffer_aml,
                          sizeof(initialization_return_short_buffer_aml));
    made_up_aml_scope(&initialization_aml, "_SB_", &system_bus);
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test,
                      initialization_counts.platform_capabilities ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_FAILED &&
                          initialization_counts.first_failure == INITIALIZATION_STATUS(TYPE_MISMATCH),
                      "an _OSC that returns a Buffer of one DWORD fails as a type mismatch");
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
 *        Intel's UUID, a commit, or its _PDC with a buffer of revision 1 and one DWORD when it has no _OSC,
 *        inside a Device that has neither too; one bound to no slot, and Devices that are no processor, a
 *        PCI bridge and an ambient light sensor with the same _UID, get nothing (§3.8 step 7, §3.10).
 *
 * Its driver_capabilities array is the _PDC buffer the test expects, not AML.
 */
KERNEL_TEST(every_bound_processor_is_given_its_c_states_through_osc_or_pdc)
{
    static const uint32_t identifiers[] = {1u, 2u};
    static const uint8_t driver_capabilities[] = {1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0x18u, 0u, 0u, 0u};
    uint8_t uuid[16];

    initialization_begin();
    initialization_slots(identifiers, 2u);
    initialization_capabilities_names(&initialization_aml, 'A');
    initialization_capabilities_names(&initialization_aml, 'C');
    initialization_capabilities_names(&initialization_aml, 'N');
    initialization_capabilities_names(&initialization_aml, 'S');
    made_up_aml_name_integer(&initialization_aml, "XPDC", 0u);
    made_up_aml_name_integer(&initialization_aml, "BPDC", 0u);
    made_up_aml_begin(&initialization_inner);
    initialization_capabilities_recorder(&initialization_inner, 'A', 3u, 0u);
    initialization_driver_capabilities_recorder(&initialization_inner, 'X', 9u);
    made_up_aml_processor(&initialization_aml, "CPU0", 1u, 0x410u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_hardware_identifier(&initialization_inner, "ACPI0007");
    made_up_aml_raw(&initialization_inner, initialization_unique_identifier_aml,
                    sizeof(initialization_unique_identifier_aml));
    initialization_driver_capabilities_recorder(&initialization_inner, 'B', 4u);
    made_up_aml_begin(&initialization_outer);
    made_up_aml_device(&initialization_outer, "CPU1", &initialization_inner);
    made_up_aml_device(&initialization_aml, "SCK0", &initialization_outer);
    made_up_aml_begin(&initialization_inner);
    initialization_capabilities_recorder(&initialization_inner, 'N', 5u, 0u);
    made_up_aml_processor(&initialization_aml, "CPU9", 9u, 0x410u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_raw(&initialization_inner, initialization_bridge_identifier_aml,
                    sizeof(initialization_bridge_identifier_aml));
    initialization_capabilities_recorder(&initialization_inner, 'C', 6u, 0u);
    made_up_aml_device(&initialization_aml, "PCI0", &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_hardware_identifier(&initialization_inner, "ACPI0008");
    made_up_aml_raw(&initialization_inner, initialization_unique_identifier_aml,
                    sizeof(initialization_unique_identifier_aml));
    initialization_capabilities_recorder(&initialization_inner, 'S', 7u, 0u);
    made_up_aml_device(&initialization_aml, "ALSD", &initialization_inner);
    if (!initialization_run(test, true))
        return;
    initialization_uuid("4077A616-290C-47BE-9EBD-D87058713953", uuid);
    kernel_test_check(test, initialization_integer("TRAC") == 0x34u,
                      "CPU0's _OSC (3), not its _PDC, then CPU1's _PDC (4); not CPU9, PCI0 or ALSD");
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

/**
 * @brief The handshake releases every Buffer it builds, whatever the firmware answers: an _OSC that
 *        gives Arg3 back, one that fails, and a _PDC leave as many objects alive as there were before
 *        (§3.8 steps 6 and 7).
 */
KERNEL_TEST(the_handshake_releases_every_buffer_it_builds)
{
    static MadeUpAml_t system_bus;
    static const uint32_t identifiers[] = {1u, 2u, 3u};
    static const MadeUpAml_t nothing = {0};
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap = &initialization_load.heap;

    initialization_begin();
    initialization_slots(identifiers, 3u);
    made_up_aml_begin(&system_bus);
    initialization_method(&system_bus, "_OSC", 0x04u, initialization_return_arg3_aml,
                          sizeof(initialization_return_arg3_aml));
    made_up_aml_scope(&initialization_aml, "_SB_", &system_bus);
    made_up_aml_begin(&initialization_inner);
    initialization_method(&initialization_inner, "_OSC", 0x04u, initialization_return_arg3_aml,
                          sizeof(initialization_return_arg3_aml));
    made_up_aml_processor(&initialization_aml, "CPU0", 1u, 0x410u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    made_up_aml_method(&initialization_inner, "_PDC", 0x01u, &nothing);
    made_up_aml_processor(&initialization_aml, "CPU1", 2u, 0x410u, 6u, &initialization_inner);
    made_up_aml_begin(&initialization_inner);
    initialization_failing(&initialization_inner, "_OSC", 0x04u, true);
    made_up_aml_processor(&initialization_aml, "CPU2", 3u, 0x410u, 6u, &initialization_inner);
    if (!initialization_run(test, false))
        return;

    const uint32_t alive = heap->objects - heap->objects_freed;

    advanced_configuration_and_power_interface_initialize_namespace(&initialization_load, &initialization_machine,
                                                                    &initialization_counts, &initialization_idle);
    kernel_test_check(test,
                      initialization_counts.platform_capabilities ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_GRANTED &&
                          initialization_counts.processor_capabilities == 3u &&
                          initialization_counts.processor_capabilities_failed == 1u,
                      "the platform granted, three processors handed the capabilities, one failed");
    kernel_test_check(test, heap->objects - heap->objects_freed == alive,
                      "as many objects alive after the handshake as before it");
    initialization_close(test);
}

/**
 * @brief AMD's _PIC, which rewrites the links of the PCI interrupt router when its argument is 1, is not called by an
 *        initialisation that keeps the 8259: no port of the router is written, and the deferral is counted (§3.8 step
 *        5). Called with 0 it writes no link; called with 1, as a switch to the APIC would, it writes 0x1F to each of
 *        the four links through the index and data ports.
 */
KERNEL_TEST(an_amd_interrupt_model_rewrites_its_links_only_when_given_one)
{
    initialization_begin();
    made_up_aml_raw(&initialization_aml, initialization_amd_interrupt_model_aml,
                    sizeof(initialization_amd_interrupt_model_aml));
    if (!initialization_run(test, true))
        return;
    kernel_test_check(test,
                      initialization_integer("PICM") == 0u && initialization_counts.pic_mode_deferred == 1u &&
                          initialization_port_writes(INITIALIZATION_ROUTER_INDEX, 0x100u) == 0u &&
                          initialization_port_writes(INITIALIZATION_ROUTER_DATA, 0x100u) == 0u,
                      "the initialisation leaves _PIC uncalled, counts its deferral, and writes no link");
    kernel_test_check(test,
                      initialization_interrupt_model(0u) == INITIALIZATION_STATUS(OK) &&
                          initialization_integer("PICM") == 0u &&
                          initialization_port_writes(INITIALIZATION_ROUTER_DATA, 0x100u) == 0u,
                      "_PIC (0) keeps the 8259's model and writes no link");
    kernel_test_check(
        test, initialization_interrupt_model(1u) == INITIALIZATION_STATUS(OK) && initialization_integer("PICM") == 1u,
        "_PIC (1) runs and keeps the APIC's model in PICM");
    kernel_test_check(test,
                      initialization_port_writes(INITIALIZATION_ROUTER_INDEX, 0u) == 1u &&
                          initialization_port_writes(INITIALIZATION_ROUTER_INDEX, 1u) == 1u &&
                          initialization_port_writes(INITIALIZATION_ROUTER_INDEX, 2u) == 1u &&
                          initialization_port_writes(INITIALIZATION_ROUTER_INDEX, 3u) == 1u &&
                          initialization_port_writes(INITIALIZATION_ROUTER_DATA, 0x1Fu) == 4u &&
                          initialization_host.access_count <=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES,
                      "it selects links 0 to 3 and writes 0x1F to each");
    initialization_close(test);
}

/**
 * @brief Intel's GCAP, which CPU0's _PDC calls, Loads its HWP table from the place an NVS region names only when the
 *        platform _OSC granted bit 6: a platform that grants what it is asked, which is never bit 6, leaves the table
 *        unloaded; one that grants bit 6 unasked has it loaded at step 7 and declared (§3.8).
 */
KERNEL_TEST(an_intel_capabilities_method_loads_its_hwp_table_only_when_the_platform_grants_bit_6)
{
    if (!initialization_run_hardware_p_states(test, 0u))
        return;
    kernel_test_check(test,
                      initialization_counts.processor_capabilities == 1u &&
                          initialization_counts.processor_capabilities_legacy == 1u,
                      "CPU0's _PDC is called");
    kernel_test_check(test,
                      (initialization_integer("OSCP") & 0x40u) == 0u && initialization_integer("HWPT") == 0xDEADu &&
                          initialization_load.tables_loaded == 0u,
                      "bit 6 not granted: the HWP table is not loaded");
    initialization_close(test);
    if (!initialization_run_hardware_p_states(test, 0x40u))
        return;
    kernel_test_check(test,
                      (initialization_integer("OSCP") & 0x40u) != 0u && initialization_integer("HWPT") == 1u &&
                          initialization_load.tables_loaded == 1u,
                      "bit 6 granted: GCAP loads the HWP table, which declares HWPT");
    initialization_close(test);
}
