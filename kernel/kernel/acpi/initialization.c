#include <kernel/acpi/initialization.h>

#include <string.h>

#define INITIALIZATION_OK          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define INITIALIZATION_ROOT        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT
#define INITIALIZATION_NODE(type)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define INITIALIZATION_HOLDS       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_HOLDS_INITIALIZATION
#define INITIALIZATION_CAPABLE     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_HOLDS_CAPABILITIES
#define INITIALIZATION_PRESENT     0x01u /**< _STA bit 0: the device is present. */
#define INITIALIZATION_ENABLED     0x02u /**< _STA bit 1: the device is enabled and decodes its resources. */
#define INITIALIZATION_FUNCTIONING 0x08u /**< _STA bit 3: the device works. */
#define INITIALIZATION_NO_STATUS   0x0Fu /**< What a device without _STA reads as (ACPI 6.6 §6.3.7). */
#define INITIALIZATION_CONNECT     1u    /**< _REG's Arg1: the space's handler is connected. */

#define INITIALIZATION_VALUE(kind)      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind
#define INITIALIZATION_PLATFORM(answer) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_##answer

#define INITIALIZATION_REVISION        1u     /**< The revision of every capabilities buffer the sequence passes. */
#define INITIALIZATION_QUERY           0x01u  /**< _OSC DWORD 1 bit 0: the call only asks what is granted. */
#define INITIALIZATION_REFUSED         0x0Eu  /**< _OSC DWORD 1 bits 1 to 3: failed, UUID or revision unknown. */
#define INITIALIZATION_PLATFORM_CLAIMS 0x00u  /**< The DWORD 2 bits \_SB._OSC claims: none (§3.8 step 6). */
#define INITIALIZATION_C_STATES        0x18u  /**< Intel's bits 3 and 4: independent C1, P and T, and C2 and C3. */
#define INITIALIZATION_MONITOR_WAIT    0x300u /**< Intel's bits 8 and 9: MWAIT for C1, and for C2 and C3. */

#if defined(LPL_KERNEL_REAL_TIME_MODE)
#    define INITIALIZATION_BOOT_MAX_NODES ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OTHER_MAX_NODES
#    define INITIALIZATION_BOOT_MAX_ARENA_BYTES                                                                        \
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OTHER_MAX_ARENA_BYTES
#else
#    define INITIALIZATION_BOOT_MAX_NODES ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_NODES
#    define INITIALIZATION_BOOT_MAX_ARENA_BYTES                                                                        \
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_ARENA_BYTES
#endif

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t InitializationStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t InitializationLoad_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t InitializationNode_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t InitializationValue_t;
typedef AdvancedConfigurationAndPowerInterfaceInitialization_t Initialization_t;
typedef AdvancedConfigurationAndPowerInterfaceInitializationMachine_t InitializationMachine_t;

static InitializationLoad_t initialization_boot_load;
static Initialization_t initialization_boot;

/**
 * @brief The platform-wide _OSC's UUID, 0811B06E-4A27-44F9-8D60-3CBBC22E7B48, as ToUUID lays it out.
 */
static const uint8_t initialization_platform_uuid[16] = {0x6E, 0xB0, 0x11, 0x08, 0x27, 0x4A, 0xF9, 0x44,
                                                         0x8D, 0x60, 0x3C, 0xBB, 0xC2, 0x2E, 0x7B, 0x48};

/**
 * @brief Intel's processor _OSC UUID, 4077A616-290C-47BE-9EBD-D87058713953, as ToUUID lays it out.
 */
static const uint8_t initialization_processor_uuid[16] = {0x16, 0xA6, 0x77, 0x40, 0x0C, 0x29, 0xBE, 0x47,
                                                          0x9E, 0xBD, 0xD8, 0x70, 0x58, 0x71, 0x39, 0x53};

/**
 * @brief The four characters of a NameSeg as an integer, as the namespace keys it.
 */
static uint32_t initialization_name(const char *characters)
{
    return advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) characters, 0u);
}

/**
 * @brief The node of the load's namespace at @p index.
 */
static InitializationNode_t *initialization_node(const InitializationLoad_t *load, uint32_t index)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);
}

/**
 * @brief The object the child of @p scope named @p name holds, or 0.
 */
static uint32_t initialization_object(InitializationLoad_t *load, uint32_t scope, const char *name)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_find_object(&load->space, scope,
                                                                                             initialization_name(name));
}

/**
 * @brief The node after @p index in a depth-first walk in declaration order, its children skipped
 *        unless @p descend holds; 0 once the walk is back at the root.
 */
static uint32_t initialization_next(const InitializationLoad_t *load, uint32_t index, bool descend)
{
    if (descend && initialization_node(load, index)->first_child != 0u)
        return initialization_node(load, index)->first_child;
    while (index != INITIALIZATION_ROOT && initialization_node(load, index)->next_sibling == 0u)
        index = initialization_node(load, index)->parent;
    return (index == INITIALIZATION_ROOT) ? 0u : initialization_node(load, index)->next_sibling;
}

/**
 * @brief Whether a node is one the walk of step 4 initialises: a Device, a Processor or a ThermalZone,
 *        as ACPICA and uACPI walk them.
 */
static bool initialization_is_device(const InitializationNode_t *node)
{
    return node->type == INITIALIZATION_NODE(DEVICE) || node->type == INITIALIZATION_NODE(PROCESSOR) ||
           node->type == INITIALIZATION_NODE(THERMAL_ZONE);
}

/**
 * @brief Counts a failed evaluation of @p object under its status, the first one named.
 */
static void initialization_count_failure(Initialization_t *initialization, uint32_t object,
                                         InitializationStatus_t status)
{
    if (initialization->first_failure_node == 0u)
    {
        initialization->first_failure_node = object;
        initialization->first_failure = (uint8_t) status;
    }
    ++initialization->statuses[status];
}

/**
 * @brief Evaluates @p object with @p arguments and gives back its status, its result released.
 */
static InitializationStatus_t initialization_evaluate(InitializationLoad_t *load, uint32_t object,
                                                      const InitializationValue_t *arguments, uint32_t argument_count)
{
    InitializationValue_t result;
    const InitializationStatus_t status = advanced_configuration_and_power_interface_machine_language_evaluate_node(
        load, object, arguments, argument_count, &result);

    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &result);
    return status;
}

/**
 * @brief Runs the _INI of @p scope when it has one, counted.
 */
static void initialization_run_ini(InitializationLoad_t *load, Initialization_t *initialization, uint32_t scope)
{
    const uint32_t method = initialization_object(load, scope, "_INI");

    if (method == 0u)
        return;
    ++initialization->initializations;

    const InitializationStatus_t status = initialization_evaluate(load, method, NULL, 0u);

    if (status == INITIALIZATION_OK)
        return;
    ++initialization->initialization_failed;
    initialization_count_failure(initialization, method, status);
}

/**
 * @brief Whether a sibling declared before @p region holds a PCI_Config region its scope's _REG
 *        registers: that scope has registered already.
 */
static bool initialization_scope_registered(InitializationLoad_t *load, uint32_t region)
{
    for (uint32_t sibling = initialization_node(load, initialization_node(load, region)->parent)->first_child;
         sibling != region; sibling = initialization_node(load, sibling)->next_sibling)
        if (advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_registration(
                load, sibling) != 0u)
            return true;
    return false;
}

/**
 * @brief Runs _REG (PCI_Config, 1) once in every scope that declares a PCI_Config region and holds a
 *        _REG, at its first such region in declaration order (§3.8 step 3), then lets the regions
 *        methods create register at once.
 */
static void initialization_register(InitializationLoad_t *load, Initialization_t *initialization)
{
    const InitializationValue_t arguments[2] = {
        {.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
         .payload =
             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PERIPHERAL_COMPONENT_INTERCONNECT_CONFIGURATION},
        {.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
         .payload = INITIALIZATION_CONNECT                                                                              },
    };

    for (uint32_t index = initialization_next(load, INITIALIZATION_ROOT, true); index != 0u;
         index = initialization_next(load, index, true))
    {
        const uint32_t method =
            advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_registration(
                load, index);

        if (method == 0u || initialization_scope_registered(load, index))
            continue;
        ++initialization->registrations;

        const InitializationStatus_t status = initialization_evaluate(load, method, arguments, 2u);

        if (status != INITIALIZATION_OK)
        {
            ++initialization->region_registration_failed;
            initialization_count_failure(initialization, method, status);
        }
    }
    advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_registered(load);
}

/**
 * @brief Gives @p index and every node above it the node flag @p flag.
 */
static void initialization_mark(InitializationLoad_t *load, uint32_t index, uint8_t flag)
{
    for (; index != 0u && (initialization_node(load, index)->flags & flag) == 0u;
         index = initialization_node(load, index)->parent)
        initialization_node(load, index)->flags |= flag;
}

/**
 * @brief Whether @p index has an _OSC or a _PDC.
 */
static bool initialization_holds_capabilities(InitializationLoad_t *load, uint32_t index)
{
    return initialization_object(load, index, "_OSC") != 0u || initialization_object(load, index, "_PDC") != 0u;
}

/**
 * @brief Whether the pre-pass marked @p index as a device or processor with an _OSC or a _PDC, or one
 *        above such a node.
 */
static bool initialization_capable(const InitializationLoad_t *load, uint32_t index)
{
    return (initialization_node(load, index)->flags & INITIALIZATION_CAPABLE) != 0u;
}

/**
 * @brief The pre-pass of steps 4 and 7, as ACPICA's AcpiNsFindIniMethods: marks every device, processor
 *        and thermal zone that holds an _INI, and every device and processor with an _OSC or a _PDC, each
 *        with every node above it, and counts them.
 */
static void initialization_find_initializations(InitializationLoad_t *load, Initialization_t *initialization)
{
    for (uint32_t index = initialization_next(load, INITIALIZATION_ROOT, true); index != 0u;
         index = initialization_next(load, index, true))
    {
        const InitializationNode_t *node = initialization_node(load, index);

        if (!initialization_is_device(node))
            continue;
        ++initialization->devices;
        if (initialization_object(load, index, "_INI") != 0u)
            initialization_mark(load, index, INITIALIZATION_HOLDS);
        if (node->type != INITIALIZATION_NODE(THERMAL_ZONE) && initialization_holds_capabilities(load, index))
            initialization_mark(load, index, INITIALIZATION_CAPABLE);
    }
}

/**
 * @brief Whether a _STA result is one ACPI 6.6 §6.3.7 declares invalid: not present but enabled, or
 *        present, not enabled and functioning.
 */
static bool initialization_status_invalid(uint64_t status)
{
    return (status & (INITIALIZATION_PRESENT | INITIALIZATION_ENABLED)) == INITIALIZATION_ENABLED ||
           (status & (INITIALIZATION_PRESENT | INITIALIZATION_ENABLED | INITIALIZATION_FUNCTIONING)) ==
               (INITIALIZATION_PRESENT | INITIALIZATION_FUNCTIONING);
}

/**
 * @brief Reads a device's _STA into @p out_status: 0x0F without one, a String or a Buffer converted as
 *        an operand is (§5.2); a result ACPI 6.6 §6.3.7 calls invalid is counted and kept as it is.
 *
 * @return false when its evaluation failed or gave no Integer, String or Buffer, counted.
 */
static bool initialization_read_status(InitializationLoad_t *load, Initialization_t *initialization, uint32_t device,
                                       uint64_t *out_status)
{
    const uint32_t object = initialization_object(load, device, "_STA");
    InitializationValue_t result;
    bool forbidden = false;

    *out_status = INITIALIZATION_NO_STATUS;
    if (object == 0u)
        return true;
    ++initialization->status_evaluations;

    InitializationStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate_node(load, object, NULL, 0u, &result);

    if (status == INITIALIZATION_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_to_integer(
            &load->heap, &result, (load->integer_bits == 32u) ? 32u : 64u, out_status, &forbidden);
    load->evaluation.forbidden_conversions += forbidden ? 1u : 0u;
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &result);
    if (status != INITIALIZATION_OK)
    {
        ++initialization->status_failed;
        initialization_count_failure(initialization, object, status);
        return false;
    }
    if (initialization_status_invalid(*out_status))
        ++initialization->status_invalid;
    return true;
}

/**
 * @brief Whether a node is \_SB_, whose _INI step 2 has run already.
 */
static bool initialization_is_system_bus(const InitializationLoad_t *load, uint32_t index)
{
    const InitializationNode_t *node = initialization_node(load, index);

    return node->parent == INITIALIZATION_ROOT && node->name == initialization_name("_SB_");
}

/**
 * @brief Initialises one device, processor or thermal zone (§3.8 step 4): present, its _INI runs,
 *        but for \_SB_'s; a subtree without _INI, or a device neither present nor functioning, is not
 *        walked.
 *
 * @return Whether the walk descends into its children.
 */
static bool initialization_initialize_device(InitializationLoad_t *load, Initialization_t *initialization,
                                             uint32_t device)
{
    uint64_t status = 0u;

    if ((initialization_node(load, device)->flags & INITIALIZATION_HOLDS) == 0u)
        return false;
    if (!initialization_read_status(load, initialization, device, &status))
        return true;
    if ((status & INITIALIZATION_PRESENT) == 0u)
        return (status & INITIALIZATION_FUNCTIONING) != 0u;
    if (!initialization_is_system_bus(load, device))
        initialization_run_ini(load, initialization, device);
    return true;
}

/**
 * @brief Walks the devices, processors and thermal zones in declaration order, children after their
 *        parent (§3.8 step 4).
 */
static void initialization_walk(InitializationLoad_t *load, Initialization_t *initialization)
{
    uint32_t index = initialization_next(load, INITIALIZATION_ROOT, true);

    while (index != 0u)
    {
        const bool descend = !initialization_is_device(initialization_node(load, index)) ||
                             initialization_initialize_device(load, initialization, index);

        index = initialization_next(load, index, descend);
    }
}

/**
 * @brief Writes @p value at @p bytes, little-endian.
 */
static void initialization_put_double_word(uint8_t *bytes, uint32_t value)
{
    for (uint32_t index = 0u; index < 4u; ++index)
        bytes[index] = (uint8_t) (value >> (8u * index));
}

/**
 * @brief The little-endian DWORD at @p bytes.
 */
static uint32_t initialization_double_word(const uint8_t *bytes)
{
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8u) | ((uint32_t) bytes[2] << 16u) |
           ((uint32_t) bytes[3] << 24u);
}

/**
 * @brief Gives in @p out_value a Buffer of the @p length bytes at @p bytes.
 */
static InitializationStatus_t initialization_buffer(InitializationLoad_t *load, const uint8_t *bytes, uint32_t length,
                                                    InitializationValue_t *out_value)
{
    const InitializationStatus_t status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        &load->heap, &load->space, INITIALIZATION_VALUE(BUFFER), length, out_value);

    if (status == INITIALIZATION_OK)
        memcpy(
            advanced_configuration_and_power_interface_machine_language_object_payload(&load->heap, out_value->handle),
            bytes, length);
    return status;
}

/**
 * @brief Evaluates an _OSC with @p uuid, revision 1 and the two DWORDs @p query and @p capabilities,
 *        and gives in @p out_returned the two it returns; a result other than a Buffer of at least 8
 *        bytes is TYPE_MISMATCH.
 */
static InitializationStatus_t initialization_operating_system_capabilities(InitializationLoad_t *load, uint32_t method,
                                                                           const uint8_t uuid[16], uint32_t query,
                                                                           uint32_t capabilities,
                                                                           uint32_t out_returned[2])
{
    uint8_t double_words[8];
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    InitializationValue_t result = {0};
    InitializationValue_t arguments[4] = {
        {0},
        {.kind = INITIALIZATION_VALUE(INTEGER), .payload = INITIALIZATION_REVISION},
        {.kind = INITIALIZATION_VALUE(INTEGER), .payload = 2u},
        {0},
    };

    initialization_put_double_word(double_words, query);
    initialization_put_double_word(double_words + 4u, capabilities);

    InitializationStatus_t status = initialization_buffer(load, uuid, 16u, &arguments[0]);

    if (status == INITIALIZATION_OK)
        status = initialization_buffer(load, double_words, sizeof(double_words), &arguments[3]);
    if (status == INITIALIZATION_OK)
        status = advanced_configuration_and_power_interface_machine_language_evaluate_node(load, method, arguments, 4u,
                                                                                           &result);
    if (status == INITIALIZATION_OK && (result.kind != INITIALIZATION_VALUE(BUFFER) ||
                                        advanced_configuration_and_power_interface_machine_language_value_bytes(
                                            &load->heap, &result, 64u, scratch, &bytes, &length) != INITIALIZATION_OK ||
                                        length < sizeof(double_words)))
        status = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH;
    if (status == INITIALIZATION_OK)
    {
        out_returned[0] = initialization_double_word(bytes);
        out_returned[1] = initialization_double_word(bytes + 4u);
    }
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &result);
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &arguments[0]);
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &arguments[3]);
    return status;
}

/**
 * @brief Asks \_SB._OSC for the DWORD 2 bits the kernel claims, none, with a query then a commit of what
 *        the query granted (§3.8 step 6), and records how it answered.
 */
static void initialization_negotiate_platform(InitializationLoad_t *load, Initialization_t *initialization)
{
    const uint32_t method =
        initialization_object(load, initialization_object(load, INITIALIZATION_ROOT, "_SB_"), "_OSC");
    uint32_t returned[2] = {0u, 0u};

    if (method == 0u)
        return;

    InitializationStatus_t status = initialization_operating_system_capabilities(
        load, method, initialization_platform_uuid, INITIALIZATION_QUERY, INITIALIZATION_PLATFORM_CLAIMS, returned);

    if (status == INITIALIZATION_OK && (returned[0] & INITIALIZATION_REFUSED) == 0u)
        status = initialization_operating_system_capabilities(load, method, initialization_platform_uuid, 0u,
                                                              returned[1] & INITIALIZATION_PLATFORM_CLAIMS, returned);
    if (status != INITIALIZATION_OK)
    {
        initialization->platform_capabilities = INITIALIZATION_PLATFORM(FAILED);
        initialization_count_failure(initialization, method, status);
        return;
    }
    initialization->platform_capabilities = ((returned[0] & INITIALIZATION_REFUSED) != 0u) ?
                                                INITIALIZATION_PLATFORM(REFUSED) :
                                                INITIALIZATION_PLATFORM(GRANTED);
}

/**
 * @brief Whether a Device or a Processor is a processor object: a Processor, or a Device whose _HID is
 *        "ACPI0007" (ACPI 6.6 §8.4); a _HID that cannot be read is counted.
 */
static bool initialization_is_processor(InitializationLoad_t *load, Initialization_t *initialization, uint32_t index)
{
    if (initialization_node(load, index)->type == INITIALIZATION_NODE(PROCESSOR))
        return true;

    const uint32_t object = initialization_object(load, index, "_HID");
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    InitializationValue_t result;

    if (object == 0u)
        return false;

    const InitializationStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate_node(load, object, NULL, 0u, &result);
    const bool processor = status == INITIALIZATION_OK && result.kind == INITIALIZATION_VALUE(STRING) &&
                           advanced_configuration_and_power_interface_machine_language_value_bytes(
                               &load->heap, &result, 64u, scratch, &bytes, &length) == INITIALIZATION_OK &&
                           length == 8u && memcmp(bytes, "ACPI0007", 8u) == 0;

    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &result);
    if (status != INITIALIZATION_OK)
        initialization_count_failure(initialization, object, status);
    return processor;
}

/**
 * @brief The ACPI Processor UID of a processor object into @p out_identifier: a Processor's ProcessorID,
 *        or a Device's _UID converted as an operand is (§3.10, §5.2); a _UID that cannot be read is
 *        counted.
 *
 * @return false when the Device has no _UID, or it cannot be read.
 */
static bool initialization_processor_identifier(InitializationLoad_t *load, Initialization_t *initialization,
                                                uint32_t index, uint64_t *out_identifier)
{
    const InitializationNode_t *node = initialization_node(load, index);

    if (node->type == INITIALIZATION_NODE(PROCESSOR))
    {
        *out_identifier = (node->value.payload >>
                           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PROCESSOR_IDENTIFIER_SHIFT) &
                          0xFFu;
        return true;
    }

    const uint32_t object = initialization_object(load, index, "_UID");
    InitializationValue_t result;
    bool forbidden = false;

    if (object == 0u)
        return false;

    InitializationStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate_node(load, object, NULL, 0u, &result);

    if (status == INITIALIZATION_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_to_integer(
            &load->heap, &result, (load->integer_bits == 32u) ? 32u : 64u, out_identifier, &forbidden);
    load->evaluation.forbidden_conversions += forbidden ? 1u : 0u;
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &result);
    if (status == INITIALIZATION_OK)
        return true;
    initialization_count_failure(initialization, object, status);
    return false;
}

/**
 * @brief Whether a CPU slot of @p machine's MADT, which holds Enabled processors only, has the ACPI
 *        Processor UID @p identifier (§3.10).
 */
static bool initialization_processor_bound(const InitializationMachine_t *machine, uint64_t identifier)
{
    if (machine->madt == NULL)
        return false;
    for (uint32_t slot = 0u; slot < machine->madt->processor_count; ++slot)
        if (machine->madt->processors[slot].processor_unique_identifier == identifier)
            return true;
    return false;
}

/**
 * @brief Evaluates a _PDC with a buffer of revision 1 that holds the one DWORD @p capabilities.
 */
static InitializationStatus_t initialization_processor_driver_capabilities(InitializationLoad_t *load, uint32_t method,
                                                                           uint32_t capabilities)
{
    uint8_t double_words[12];
    InitializationValue_t buffer = {0};

    initialization_put_double_word(double_words, INITIALIZATION_REVISION);
    initialization_put_double_word(double_words + 4u, 1u);
    initialization_put_double_word(double_words + 8u, capabilities);

    InitializationStatus_t status = initialization_buffer(load, double_words, sizeof(double_words), &buffer);

    if (status == INITIALIZATION_OK)
        status = initialization_evaluate(load, method, &buffer, 1u);
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &buffer);
    return status;
}

/**
 * @brief Gives a bound processor object @p capabilities through its _OSC with Intel's UUID, a commit
 *        without a query, or through its _PDC when it has no _OSC (§3.8 step 7), counted.
 */
static void initialization_negotiate_processor(InitializationLoad_t *load, Initialization_t *initialization,
                                               uint32_t processor, uint32_t capabilities)
{
    uint32_t method = initialization_object(load, processor, "_OSC");
    uint32_t returned[2] = {0u, 0u};
    InitializationStatus_t status = INITIALIZATION_OK;

    ++initialization->processor_capabilities;
    if (method != 0u)
    {
        status = initialization_operating_system_capabilities(load, method, initialization_processor_uuid, 0u,
                                                              capabilities, returned);
        if (status == INITIALIZATION_OK && (returned[0] & INITIALIZATION_REFUSED) != 0u)
            ++initialization->processor_capabilities_refused;
    }
    else
    {
        method = initialization_object(load, processor, "_PDC");
        ++initialization->processor_capabilities_legacy;
        status = initialization_processor_driver_capabilities(load, method, capabilities);
    }
    if (status == INITIALIZATION_OK)
        return;
    ++initialization->processor_capabilities_failed;
    initialization_count_failure(initialization, method, status);
}

/**
 * @brief Gives every processor object with an _OSC or a _PDC that binds to a CPU slot the capabilities
 *        of its C-states, and MWAIT's when the processors implement it, in declaration order (§3.8 step
 *        7), walking only the subtrees the pre-pass marked; counts the ones that bind to none.
 */
static void initialization_negotiate_processors(InitializationLoad_t *load, Initialization_t *initialization,
                                                const InitializationMachine_t *machine)
{
    const uint32_t capabilities = INITIALIZATION_C_STATES | (machine->monitor_wait ? INITIALIZATION_MONITOR_WAIT : 0u);

    if (!initialization_capable(load, INITIALIZATION_ROOT))
        return;
    for (uint32_t index = initialization_next(load, INITIALIZATION_ROOT, true); index != 0u;
         index = initialization_next(load, index, initialization_capable(load, index)))
    {
        const uint8_t type = initialization_node(load, index)->type;
        uint64_t identifier = 0u;

        if (!initialization_capable(load, index) ||
            (type != INITIALIZATION_NODE(PROCESSOR) && type != INITIALIZATION_NODE(DEVICE)) ||
            !initialization_holds_capabilities(load, index) ||
            !initialization_is_processor(load, initialization, index))
            continue;
        if (!initialization_processor_identifier(load, initialization, index, &identifier) ||
            !initialization_processor_bound(machine, identifier))
        {
            ++initialization->processors_unbound;
            continue;
        }
        initialization_negotiate_processor(load, initialization, index, capabilities);
    }
}

void advanced_configuration_and_power_interface_initialize_namespace(InitializationLoad_t *load,
                                                                     const InitializationMachine_t *machine,
                                                                     Initialization_t *out_initialization)
{
    memset(out_initialization, 0, sizeof(*out_initialization));
    initialization_find_initializations(load, out_initialization);
    initialization_run_ini(load, out_initialization, INITIALIZATION_ROOT);
    initialization_run_ini(load, out_initialization, initialization_object(load, INITIALIZATION_ROOT, "_SB_"));
    initialization_register(load, out_initialization);
    initialization_walk(load, out_initialization);
    out_initialization->pic_mode_deferred = (initialization_object(load, INITIALIZATION_ROOT, "_PIC") != 0u) ? 1u : 0u;
    initialization_negotiate_platform(load, out_initialization);
    initialization_negotiate_processors(load, out_initialization, machine);
    advanced_configuration_and_power_interface_machine_language_drop_notifications(load);
}

const char *advanced_configuration_and_power_interface_platform_capabilities_name(uint32_t capabilities)
{
    static const char *const names[] = {"absent", "granted", "refused", "failed"};

    return (capabilities < sizeof(names) / sizeof(names[0])) ? names[capabilities] : "unknown";
}

void advanced_configuration_and_power_interface_machine_language_initialize(bool monitor_wait)
{
    const InitializationMachine_t machine = {
        .madt = advanced_configuration_and_power_interface_madt_is_available() ?
                    advanced_configuration_and_power_interface_madt() :
                    NULL,
        .monitor_wait = monitor_wait,
    };

    if (advanced_configuration_and_power_interface_machine_language_open(
            &initialization_boot_load, &advanced_configuration_and_power_interface_machine_host,
            INITIALIZATION_BOOT_MAX_NODES, INITIALIZATION_BOOT_MAX_ARENA_BYTES))
    {
        advanced_configuration_and_power_interface_machine_language_load_directory(
            &initialization_boot_load, advanced_configuration_and_power_interface_directory());
        advanced_configuration_and_power_interface_initialize_namespace(&initialization_boot_load, &machine,
                                                                        &initialization_boot);
    }
}

const InitializationLoad_t *advanced_configuration_and_power_interface_machine_language(void)
{
    return &initialization_boot_load;
}

const Initialization_t *advanced_configuration_and_power_interface_initialization(void) { return &initialization_boot; }
