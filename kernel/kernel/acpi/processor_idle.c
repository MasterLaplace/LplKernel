#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/resources.h>

#include <string.h>

#define PROCESSOR_IDLE_OK             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define PROCESSOR_IDLE_ROOT           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT
#define PROCESSOR_IDLE_NODE(type)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define PROCESSOR_IDLE_VALUE(kind)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind
#define PROCESSOR_IDLE_STATUS(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define PROCESSOR_IDLE_CONTROL(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_##name
#define PROCESSOR_IDLE_MODE(outcome)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_##outcome
#define PROCESSOR_IDLE_NO_SLOT        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_NO_SLOT
#define PROCESSOR_IDLE_MAX_STATES     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_MAX_STATES
#define PROCESSOR_IDLE_MAX_SLOTS      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_PROCESSOR_COUNT
#define PROCESSOR_IDLE_MALFORMED      PROCESSOR_IDLE_STATUS(TYPE_MISMATCH) /**< What a malformed _CST package fails as. */
#define PROCESSOR_IDLE_STATE_ELEMENTS 4u       /**< A C-state's elements: Register, Type, Latency and Power. */
#define PROCESSOR_IDLE_PORTS          0x10000u /**< The I/O space: ports 0 to 0xFFFF. */
#define PROCESSOR_IDLE_SLOT_BITS      32u      /**< CPU slots one word of a slot set holds. */
#define PROCESSOR_IDLE_PRESENT        0x01u    /**< _STA bit 0: the device is present. */
#define PROCESSOR_IDLE_ENABLED        0x02u    /**< _STA bit 1: the device is enabled. */
#define PROCESSOR_IDLE_FUNCTIONING    0x08u    /**< _STA bit 3: the device works. */
#define PROCESSOR_IDLE_NO_STATUS      0x0Fu    /**< What a device without _STA reads as (ACPI 6.6 §6.3.7). */
#define PROCESSOR_IDLE_STATUS_READ    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_STATUS_READ
#define PROCESSOR_IDLE_ABSENT         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_ABSENT
#define PROCESSOR_IDLE_FOLD_OFFSET    0x811C9DC5u /**< FNV-1a's offset basis. */
#define PROCESSOR_IDLE_FOLD_PRIME     0x01000193u /**< FNV-1a's prime. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t ProcessorIdleStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t ProcessorIdleLoad_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t ProcessorIdleNode_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t ProcessorIdleValue_t;
typedef AdvancedConfigurationAndPowerInterfaceInitialization_t ProcessorIdleCounts_t;
typedef AdvancedConfigurationAndPowerInterfaceInitializationMachine_t ProcessorIdleMachine_t;
typedef AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t ProcessorIdleTable_t;
typedef AdvancedConfigurationAndPowerInterfaceProcessorIdle_t ProcessorIdle_t;
typedef AdvancedConfigurationAndPowerInterfaceIdleState_t ProcessorIdleState_t;

/**
 * @brief Folds the @p width low bytes of @p value, little-endian, into @p fold with FNV-1a.
 */
static uint32_t processor_idle_fold_unsigned(uint32_t fold, uint64_t value, uint32_t width)
{
    for (uint32_t byte = 0u; byte < width; ++byte)
        fold = (fold ^ (uint8_t) (value >> (8u * byte))) * PROCESSOR_IDLE_FOLD_PRIME;
    return fold;
}

/**
 * @brief The node of the load's namespace at @p index.
 */
static ProcessorIdleNode_t *processor_idle_node(const ProcessorIdleLoad_t *load, uint32_t index)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);
}

/**
 * @brief The object the child of @p scope named @p name holds, or 0.
 */
static uint32_t processor_idle_object(ProcessorIdleLoad_t *load, uint32_t scope, const char *name)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_find_object(
        &load->space, scope,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u));
}

/**
 * @brief Gives back what a value holds.
 */
static void processor_idle_release(ProcessorIdleLoad_t *load, ProcessorIdleValue_t *value)
{
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, value);
}

/**
 * @brief How many elements a Package holds.
 */
static uint32_t processor_idle_elements(const ProcessorIdleLoad_t *load, const ProcessorIdleValue_t *package)
{
    return advanced_configuration_and_power_interface_machine_language_object(&load->heap, package->handle)->length;
}

/**
 * @brief Reads element @p element of @p package as an Integer that fits 32 bits into @p out_value.
 */
static ProcessorIdleStatus_t processor_idle_double_word(ProcessorIdleLoad_t *load, const ProcessorIdleValue_t *package,
                                                        uint32_t element, uint32_t *out_value)
{
    ProcessorIdleValue_t value;
    ProcessorIdleStatus_t status =
        advanced_configuration_and_power_interface_machine_language_package_element(load, package, element, &value);

    if (status == PROCESSOR_IDLE_OK && (value.kind != PROCESSOR_IDLE_VALUE(INTEGER) || value.payload > 0xFFFFFFFFu))
        status = PROCESSOR_IDLE_MALFORMED;
    if (status == PROCESSOR_IDLE_OK)
        *out_value = (uint32_t) value.payload;
    processor_idle_release(load, &value);
    return status;
}

/**
 * @brief Reads the Register of a C-state: the first Generic Register of the resource template, a Buffer,
 *        its first element holds, before its End Tag (ACPI 6.6 §8.4.1.1).
 */
static ProcessorIdleStatus_t processor_idle_register(ProcessorIdleLoad_t *load, const ProcessorIdleValue_t *state,
                                                     AdvancedConfigurationAndPowerInterfaceGenericAddress_t *out_entry)
{
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    ProcessorIdleValue_t buffer;
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t iterator;
    AdvancedConfigurationAndPowerInterfaceResource_t resource;
    AdvancedConfigurationAndPowerInterfaceResourceStep_t step = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_END;
    ProcessorIdleStatus_t status =
        advanced_configuration_and_power_interface_machine_language_package_element(load, state, 0u, &buffer);

    if (status == PROCESSOR_IDLE_OK && buffer.kind != PROCESSOR_IDLE_VALUE(BUFFER))
        status = PROCESSOR_IDLE_MALFORMED;
    if (status == PROCESSOR_IDLE_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_bytes(&load->heap, &buffer, 64u,
                                                                                         scratch, &bytes, &length);
    if (status == PROCESSOR_IDLE_OK)
    {
        advanced_configuration_and_power_interface_resources_begin(&iterator, bytes, length);
        do
            step = advanced_configuration_and_power_interface_resources_next(&iterator, &resource);
        while (step == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_DESCRIPTOR &&
               !advanced_configuration_and_power_interface_resources_generic_register(&resource, out_entry));
        if (step != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_DESCRIPTOR)
            status = PROCESSOR_IDLE_MALFORMED;
    }
    processor_idle_release(load, &buffer);
    return status;
}

/**
 * @brief Reads the C-state element @p element of a _CST package holds: a Package of a Register, then
 *        the Type, the Latency and the Power, each an Integer.
 */
static ProcessorIdleStatus_t processor_idle_state(ProcessorIdleLoad_t *load, const ProcessorIdleValue_t *idle,
                                                  uint32_t element, ProcessorIdleState_t *out_state)
{
    ProcessorIdleValue_t state;
    ProcessorIdleStatus_t status =
        advanced_configuration_and_power_interface_machine_language_package_element(load, idle, element, &state);

    if (status == PROCESSOR_IDLE_OK && (state.kind != PROCESSOR_IDLE_VALUE(PACKAGE) ||
                                        processor_idle_elements(load, &state) != PROCESSOR_IDLE_STATE_ELEMENTS))
        status = PROCESSOR_IDLE_MALFORMED;
    if (status == PROCESSOR_IDLE_OK)
        status = processor_idle_register(load, &state, &out_state->entry);
    if (status == PROCESSOR_IDLE_OK)
        status = processor_idle_double_word(load, &state, 1u, &out_state->type);
    if (status == PROCESSOR_IDLE_OK)
        status = processor_idle_double_word(load, &state, 2u, &out_state->latency);
    if (status == PROCESSOR_IDLE_OK)
        status = processor_idle_double_word(load, &state, 3u, &out_state->power);
    processor_idle_release(load, &state);
    return status;
}

/**
 * @brief Reads what a _CST returned into @p out_idle: a Package of a Count of at least 1, then as many
 *        C-states, as Linux reads it. A faulty state is left out and counted on the slot; once
 *        PROCESSOR_IDLE_MAX_STATES are published, the states after them are counted and not read.
 *
 * @return MALFORMED when the package has no Integer Count, a Count below 1, or a Count other than the
 *         sub-packages after it, and publishes nothing; OK otherwise.
 */
static ProcessorIdleStatus_t processor_idle_read(ProcessorIdleLoad_t *load, ProcessorIdleCounts_t *counts,
                                                 const ProcessorIdleValue_t *idle, ProcessorIdle_t *out_idle)
{
    uint32_t count = 0u;
    ProcessorIdleStatus_t status =
        (idle->kind == PROCESSOR_IDLE_VALUE(PACKAGE) && processor_idle_elements(load, idle) != 0u) ?
            processor_idle_double_word(load, idle, 0u, &count) :
            PROCESSOR_IDLE_MALFORMED;

    if (status == PROCESSOR_IDLE_OK && (count == 0u || count != processor_idle_elements(load, idle) - 1u))
        status = PROCESSOR_IDLE_MALFORMED;
    for (uint32_t state = 0u; status == PROCESSOR_IDLE_OK && state < count; ++state)
    {
        if (out_idle->count == PROCESSOR_IDLE_MAX_STATES)
        {
            counts->idle_states_dropped += count - state;
            break;
        }
        if (processor_idle_state(load, idle, state + 1u, &out_idle->states[out_idle->count]) == PROCESSOR_IDLE_OK)
            ++out_idle->count;
        else
            ++out_idle->skipped;
    }
    return status;
}

/**
 * @brief Evaluates the _CST of the processor object @p processor and publishes its states in @p out_idle,
 *        none when the evaluation fails or its package is malformed, counted.
 */
static void processor_idle_evaluate(ProcessorIdleLoad_t *load, ProcessorIdleCounts_t *counts, uint32_t processor,
                                    ProcessorIdle_t *out_idle)
{
    const uint32_t object = processor_idle_object(load, processor, "_CST");
    ProcessorIdleValue_t result = {0};
    ProcessorIdleStatus_t status = PROCESSOR_IDLE_STATUS(NAME_NOT_FOUND);

    memset(out_idle, 0, sizeof(*out_idle));
    out_idle->processor = processor;
    ++counts->processor_idle;
    if (object != 0u)
        status =
            advanced_configuration_and_power_interface_machine_language_evaluate_node(load, object, NULL, 0u, &result);
    if (status == PROCESSOR_IDLE_OK)
        status = processor_idle_read(load, counts, &result, out_idle);
    processor_idle_release(load, &result);
    if (status == PROCESSOR_IDLE_OK)
        return;
    ++counts->processor_idle_failed;
    advanced_configuration_and_power_interface_initialization_count_failure(counts, (object != 0u) ? object : processor,
                                                                            status);
}

/**
 * @brief Whether step 0 left the platform in ACPI mode: it switched it, found it switched already, or
 *        the platform has no legacy mode to leave.
 */
static bool processor_idle_in_acpi_mode(uint8_t mode)
{
    return mode == PROCESSOR_IDLE_MODE(SWITCHED) || mode == PROCESSOR_IDLE_MODE(ALREADY) ||
           mode == PROCESSOR_IDLE_MODE(NO_LEGACY_MODE);
}

/**
 * @brief Writes CST_CNT to SMI_CMD, one byte, once the platform is in ACPI mode and SMI_CMD is a port the
 *        kernel may write (§3.8 step 8, ACPI 6.6 §5.2.9).
 *
 * @return An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_* value.
 */
static uint32_t processor_idle_claim_control(ProcessorIdleLoad_t *load, const ProcessorIdleMachine_t *machine)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = machine->fadt;

    if (fadt == NULL || fadt->idle_state_command == 0u || fadt->system_management_interrupt_command == 0u)
        return PROCESSOR_IDLE_CONTROL(ABSENT);
    if (!processor_idle_in_acpi_mode(machine->mode) ||
        fadt->system_management_interrupt_command >= PROCESSOR_IDLE_PORTS ||
        advanced_configuration_and_power_interface_port_is_protected(
            (uint16_t) fadt->system_management_interrupt_command))
        return PROCESSOR_IDLE_CONTROL(SKIPPED);
    load->space.host->port_write(load->space.host->context, (uint16_t) fadt->system_management_interrupt_command, 1u,
                                 fadt->idle_state_command);
    return PROCESSOR_IDLE_CONTROL(WRITTEN);
}

/**
 * @brief The CPU slot whose published _CST is @p processor's, or PROCESSOR_IDLE_NO_SLOT.
 */
static uint32_t processor_idle_published_slot(const ProcessorIdleTable_t *table, uint32_t processor)
{
    for (uint32_t slot = 0u; slot < table->slot_count; ++slot)
        if (table->slots[slot].processor == processor)
            return slot;
    return PROCESSOR_IDLE_NO_SLOT;
}

/**
 * @brief Takes every Notify queued: a Notify 0x81 on a processor object whose _CST is published already
 *        marks its slot in @p changed; every other one is dropped.
 */
static void processor_idle_take_notifications(ProcessorIdleLoad_t *load, const ProcessorIdleTable_t *table,
                                              uint32_t *changed)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNotification_t notification;

    while (advanced_configuration_and_power_interface_machine_language_take_notification(load, &notification))
    {
        const uint32_t slot =
            (notification.value == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_CHANGED) ?
                processor_idle_published_slot(table, notification.node) :
                PROCESSOR_IDLE_NO_SLOT;

        if (slot != PROCESSOR_IDLE_NO_SLOT)
            changed[slot / PROCESSOR_IDLE_SLOT_BITS] |= 1u << (slot % PROCESSOR_IDLE_SLOT_BITS);
    }
}

/**
 * @brief Evaluates @p object and converts what it gives into @p out_value, as an operand is converted
 *        (§5.2); a failure is counted.
 *
 * @return Whether it could.
 */
static bool processor_idle_integer(ProcessorIdleLoad_t *load, ProcessorIdleCounts_t *counts, uint32_t object,
                                   uint64_t *out_value)
{
    ProcessorIdleValue_t result;
    bool forbidden = false;
    ProcessorIdleStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate_node(load, object, NULL, 0u, &result);

    if (status == PROCESSOR_IDLE_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_to_integer(
            &load->heap, &result, (load->integer_bits == 32u) ? 32u : 64u, out_value, &forbidden);
    load->evaluation.forbidden_conversions += forbidden ? 1u : 0u;
    processor_idle_release(load, &result);
    if (status == PROCESSOR_IDLE_OK)
        return true;
    advanced_configuration_and_power_interface_initialization_count_failure(counts, object, status);
    return false;
}

/**
 * @brief What the _STA of @p node reads as Linux reads it (v7.3 acpi_bus_get_status_handle,
 *        acpi_scan_init_status): 0x0F without one, 0 when it cannot be read.
 */
static uint64_t processor_idle_status(ProcessorIdleLoad_t *load, ProcessorIdleCounts_t *counts, uint32_t node)
{
    const uint32_t object = processor_idle_object(load, node, "_STA");
    uint64_t status = PROCESSOR_IDLE_NO_STATUS;

    if (object != 0u && !processor_idle_integer(load, counts, object, &status))
        status = 0u;
    return status;
}

/**
 * @brief Whether a node is one Linux makes a device of, whose _STA it reads: a Device, a Processor or a
 *        ThermalZone.
 */
static bool processor_idle_is_device(const ProcessorIdleNode_t *node)
{
    return node->type == PROCESSOR_IDLE_NODE(DEVICE) || node->type == PROCESSOR_IDLE_NODE(PROCESSOR) ||
           node->type == PROCESSOR_IDLE_NODE(THERMAL_ZONE);
}

/**
 * @brief Whether a device above @p node is neither present nor functioning, as Linux attaches nothing
 *        below such a device (v7.3 acpi_bus_attach). Each device's _STA is read once, the topmost first,
 *        and kept in its node's flags.
 */
static bool processor_idle_hidden(ProcessorIdleLoad_t *load, ProcessorIdleCounts_t *counts, uint32_t node)
{
    for (;;)
    {
        uint32_t unread = 0u;

        for (uint32_t index = processor_idle_node(load, node)->parent; index != PROCESSOR_IDLE_ROOT;
             index = processor_idle_node(load, index)->parent)
        {
            const ProcessorIdleNode_t *above = processor_idle_node(load, index);

            if (!processor_idle_is_device(above))
                continue;
            if ((above->flags & PROCESSOR_IDLE_STATUS_READ) == 0u)
                unread = index;
            else if ((above->flags & PROCESSOR_IDLE_ABSENT) != 0u)
                return true;
        }
        if (unread == 0u)
            return false;
        processor_idle_node(load, unread)->flags |= PROCESSOR_IDLE_STATUS_READ;
        if ((processor_idle_status(load, counts, unread) & (PROCESSOR_IDLE_PRESENT | PROCESSOR_IDLE_FUNCTIONING)) == 0u)
            processor_idle_node(load, unread)->flags |= PROCESSOR_IDLE_ABSENT;
    }
}

/**
 * @brief Whether Linux would attach the processor object @p processor (v7.3 acpi_bus_attach,
 *        acpi_processor_add): no device above it hidden, and its own _STA present or functioning, and
 *        enabled.
 */
static bool processor_idle_present(ProcessorIdleLoad_t *load, ProcessorIdleCounts_t *counts, uint32_t processor)
{
    if (processor_idle_hidden(load, counts, processor))
        return false;

    const uint64_t status = processor_idle_status(load, counts, processor);

    return (status & (PROCESSOR_IDLE_PRESENT | PROCESSOR_IDLE_FUNCTIONING)) != 0u &&
           (status & PROCESSOR_IDLE_ENABLED) != 0u;
}

/**
 * @brief Whether a node may be a processor object with a _CST: a Processor, or a Device, that holds one.
 */
static bool processor_idle_candidate(ProcessorIdleLoad_t *load, uint32_t index)
{
    const uint8_t type = processor_idle_node(load, index)->type;

    return (type == PROCESSOR_IDLE_NODE(PROCESSOR) || type == PROCESSOR_IDLE_NODE(DEVICE)) &&
           processor_idle_object(load, index, "_CST") != 0u;
}

/**
 * @brief Step 8 of §3.8: publishes the _CST of each processor object _STA keeps in and bound to a CPU slot
 *        no earlier object took, in declaration order, the Notifies queued before each evaluation taken
 *        first.
 */
static void processor_idle_publish_slots(ProcessorIdleLoad_t *load, const ProcessorIdleMachine_t *machine,
                                         ProcessorIdleCounts_t *counts, ProcessorIdleTable_t *table, uint32_t *changed)
{
    for (uint32_t index = advanced_configuration_and_power_interface_machine_language_namespace_next(
             &load->space, PROCESSOR_IDLE_ROOT, true);
         index != 0u;
         index = advanced_configuration_and_power_interface_machine_language_namespace_next(&load->space, index, true))
    {
        if (!processor_idle_candidate(load, index) ||
            !advanced_configuration_and_power_interface_processor_object(load, counts, index))
            continue;
        if (!processor_idle_present(load, counts, index))
        {
            ++counts->processor_idle_absent;
            continue;
        }

        const uint32_t slot = advanced_configuration_and_power_interface_processor_slot(load, machine, counts, index);

        if (slot == PROCESSOR_IDLE_NO_SLOT || table->slots[slot].processor != 0u)
        {
            ++counts->processor_idle_unbound;
            continue;
        }
        processor_idle_take_notifications(load, table, changed);
        processor_idle_evaluate(load, counts, index, &table->slots[slot]);
    }
    processor_idle_take_notifications(load, table, changed);
}

bool advanced_configuration_and_power_interface_processor_object(ProcessorIdleLoad_t *load,
                                                                 ProcessorIdleCounts_t *initialization, uint32_t node)
{
    if (processor_idle_node(load, node)->type == PROCESSOR_IDLE_NODE(PROCESSOR))
        return true;

    const uint32_t object = processor_idle_object(load, node, "_HID");
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    ProcessorIdleValue_t result;

    if (object == 0u)
        return false;

    const ProcessorIdleStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate_node(load, object, NULL, 0u, &result);
    const bool processor = status == PROCESSOR_IDLE_OK && result.kind == PROCESSOR_IDLE_VALUE(STRING) &&
                           advanced_configuration_and_power_interface_machine_language_value_bytes(
                               &load->heap, &result, 64u, scratch, &bytes, &length) == PROCESSOR_IDLE_OK &&
                           length == 8u && memcmp(bytes, "ACPI0007", 8u) == 0;

    processor_idle_release(load, &result);
    if (status != PROCESSOR_IDLE_OK)
        advanced_configuration_and_power_interface_initialization_count_failure(initialization, object, status);
    return processor;
}

uint32_t advanced_configuration_and_power_interface_processor_slot(ProcessorIdleLoad_t *load,
                                                                   const ProcessorIdleMachine_t *machine,
                                                                   ProcessorIdleCounts_t *initialization, uint32_t node)
{
    const ProcessorIdleNode_t *processor = processor_idle_node(load, node);
    uint64_t identifier = 0u;

    if (processor->type == PROCESSOR_IDLE_NODE(PROCESSOR))
        identifier = (processor->value.payload >>
                      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PROCESSOR_IDENTIFIER_SHIFT) &
                     0xFFu;
    else
    {
        const uint32_t object = processor_idle_object(load, node, "_UID");

        if (object == 0u || !processor_idle_integer(load, initialization, object, &identifier))
            return PROCESSOR_IDLE_NO_SLOT;
    }
    for (uint32_t slot = 0u; machine->madt != NULL && slot < machine->madt->processor_count; ++slot)
        if (machine->madt->processors[slot].processor_unique_identifier == identifier)
            return slot;
    return PROCESSOR_IDLE_NO_SLOT;
}

void advanced_configuration_and_power_interface_processor_idle_publish(ProcessorIdleLoad_t *load,
                                                                       const ProcessorIdleMachine_t *machine,
                                                                       ProcessorIdleCounts_t *initialization,
                                                                       ProcessorIdleTable_t *out_table)
{
    uint32_t changed[(PROCESSOR_IDLE_MAX_SLOTS + PROCESSOR_IDLE_SLOT_BITS - 1u) / PROCESSOR_IDLE_SLOT_BITS] = {0u};

    out_table->slot_count = (machine->madt != NULL) ? machine->madt->processor_count : 0u;
    memset(out_table->slots, 0, out_table->slot_count * sizeof(out_table->slots[0]));
    initialization->idle_control = processor_idle_claim_control(load, machine);
    processor_idle_publish_slots(load, machine, initialization, out_table, changed);
    for (uint32_t slot = 0u; slot < out_table->slot_count; ++slot)
    {
        if ((changed[slot / PROCESSOR_IDLE_SLOT_BITS] & (1u << (slot % PROCESSOR_IDLE_SLOT_BITS))) == 0u)
            continue;
        ++initialization->processor_idle_changes;
        processor_idle_evaluate(load, initialization, out_table->slots[slot].processor, &out_table->slots[slot]);
    }
    advanced_configuration_and_power_interface_machine_language_drop_notifications(load);
}

uint32_t advanced_configuration_and_power_interface_processor_idle_fold(const ProcessorIdleTable_t *table)
{
    uint32_t fold = processor_idle_fold_unsigned(PROCESSOR_IDLE_FOLD_OFFSET, table->slot_count, 4u);

    for (uint32_t slot = 0u; slot < table->slot_count; ++slot)
    {
        const ProcessorIdle_t *idle = &table->slots[slot];

        fold = processor_idle_fold_unsigned(fold, (idle->processor != 0u) ? 1u : 0u, 1u);
        fold = processor_idle_fold_unsigned(fold, idle->count, 4u);
        fold = processor_idle_fold_unsigned(fold, idle->skipped, 4u);
        for (uint32_t state = 0u; state < idle->count; ++state)
        {
            const ProcessorIdleState_t *entry = &idle->states[state];

            fold = processor_idle_fold_unsigned(fold, entry->entry.space, 1u);
            fold = processor_idle_fold_unsigned(fold, entry->entry.bit_width, 1u);
            fold = processor_idle_fold_unsigned(fold, entry->entry.bit_offset, 1u);
            fold = processor_idle_fold_unsigned(fold, entry->entry.access_size, 1u);
            fold = processor_idle_fold_unsigned(fold, entry->entry.address, 8u);
            fold = processor_idle_fold_unsigned(fold, entry->type, 4u);
            fold = processor_idle_fold_unsigned(fold, entry->latency, 4u);
            fold = processor_idle_fold_unsigned(fold, entry->power, 4u);
        }
    }
    return fold;
}

const char *advanced_configuration_and_power_interface_idle_control_name(uint32_t control)
{
    static const char *const names[] = {"absent", "written", "skipped"};

    return (control < sizeof(names) / sizeof(names[0])) ? names[control] : "unknown";
}
