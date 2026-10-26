#include <kernel/acpi/initialization.h>

#include <string.h>

#define INITIALIZATION_OK          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define INITIALIZATION_ROOT        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT
#define INITIALIZATION_NODE(type)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define INITIALIZATION_HOLDS       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_HOLDS_INITIALIZATION
#define INITIALIZATION_PRESENT     0x01u /**< _STA bit 0: the device is present. */
#define INITIALIZATION_ENABLED     0x02u /**< _STA bit 1: the device is enabled and decodes its resources. */
#define INITIALIZATION_FUNCTIONING 0x08u /**< _STA bit 3: the device works. */
#define INITIALIZATION_NO_STATUS   0x0Fu /**< What a device without _STA reads as (ACPI 6.6 §6.3.7). */
#define INITIALIZATION_CONNECT     1u    /**< _REG's Arg1: the space's handler is connected. */

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

static InitializationLoad_t initialization_boot_load;
static Initialization_t initialization_boot;

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
 * @brief Marks @p index and every node above it as holding an _INI.
 */
static void initialization_mark(InitializationLoad_t *load, uint32_t index)
{
    for (; index != 0u && (initialization_node(load, index)->flags & INITIALIZATION_HOLDS) == 0u;
         index = initialization_node(load, index)->parent)
        initialization_node(load, index)->flags |= INITIALIZATION_HOLDS;
}

/**
 * @brief The pre-pass of step 4, as ACPICA's AcpiNsFindIniMethods: marks every device, processor and
 *        thermal zone that holds an _INI, and every node above it, and counts them.
 */
static void initialization_find_initializations(InitializationLoad_t *load, Initialization_t *initialization)
{
    for (uint32_t index = initialization_next(load, INITIALIZATION_ROOT, true); index != 0u;
         index = initialization_next(load, index, true))
    {
        if (!initialization_is_device(initialization_node(load, index)))
            continue;
        ++initialization->devices;
        if (initialization_object(load, index, "_INI") != 0u)
            initialization_mark(load, index);
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
 *        an operand is (§5.2), 0 for a result ACPI declares invalid, counted.
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
    {
        ++initialization->status_invalid;
        *out_status = 0u;
    }
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

void advanced_configuration_and_power_interface_initialize_namespace(InitializationLoad_t *load,
                                                                     Initialization_t *out_initialization)
{
    memset(out_initialization, 0, sizeof(*out_initialization));
    initialization_find_initializations(load, out_initialization);
    initialization_run_ini(load, out_initialization, INITIALIZATION_ROOT);
    initialization_run_ini(load, out_initialization, initialization_object(load, INITIALIZATION_ROOT, "_SB_"));
    initialization_register(load, out_initialization);
    initialization_walk(load, out_initialization);
    out_initialization->pic_mode_deferred = (initialization_object(load, INITIALIZATION_ROOT, "_PIC") != 0u) ? 1u : 0u;
    advanced_configuration_and_power_interface_machine_language_drop_notifications(load);
}

void advanced_configuration_and_power_interface_machine_language_initialize(void)
{
    if (advanced_configuration_and_power_interface_machine_language_open(
            &initialization_boot_load, &advanced_configuration_and_power_interface_machine_host,
            INITIALIZATION_BOOT_MAX_NODES, INITIALIZATION_BOOT_MAX_ARENA_BYTES))
    {
        advanced_configuration_and_power_interface_machine_language_load_directory(
            &initialization_boot_load, advanced_configuration_and_power_interface_directory());
        advanced_configuration_and_power_interface_initialize_namespace(&initialization_boot_load,
                                                                        &initialization_boot);
    }
}

const InitializationLoad_t *advanced_configuration_and_power_interface_machine_language(void)
{
    return &initialization_boot_load;
}

const Initialization_t *advanced_configuration_and_power_interface_initialization(void) { return &initialization_boot; }
