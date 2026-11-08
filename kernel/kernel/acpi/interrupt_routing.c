#include <kernel/acpi/interrupt_routing.h>
#include <kernel/acpi/resources.h>

#include <stddef.h>
#include <string.h>

#define ROUTING_OK             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define ROUTING_ROOT           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT
#define ROUTING_NODE(type)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define ROUTING_VALUE(kind)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind
#define ROUTING_STATUS(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define ROUTING_STEP(name)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_##name
#define ROUTING_MAX_ROUTES     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_ROUTES
#define ROUTING_MAX_LINKS      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_LINKS
#define ROUTING_MALFORMED      ROUTING_STATUS(TYPE_MISMATCH) /**< What a result of the wrong shape fails as. */
#define ROUTING_ENTRY_ELEMENTS 4u      /**< A mapping package: Address, Pin, Source, SourceIndex (ACPI 6.6 §6.2.14). */
#define ROUTING_DEVICE_SHIFT   16u     /**< Where the device number starts in an Address, as in an _ADR. */
#define ROUTING_DEVICE_MASK    0xFFFFu /**< Its bits, as Linux reads them. */
#define ROUTING_DEVICES        32u     /**< Device numbers on a bus. */
#define ROUTING_PINS           4u      /**< INTA to INTD. */
#define ROUTING_PRESENT        0x01u   /**< _STA bit 0: the device is present. */
#define ROUTING_FUNCTIONING    0x08u   /**< _STA bit 3: the device works. */
#define ROUTING_HOST_BRIDGE                                                                                            \
    0x030AD041u                          /**< EisaId ("PNP0A03"): a PCI host bridge, which a PCI Express root          \
                                              complex (PNP0A08) names in its _CID. */
#define ROUTING_LINK         0x0F0CD041u /**< EisaId ("PNP0C0F"): a PCI interrupt link. */
#define ROUTING_NO_INTERRUPT ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_NO_INTERRUPT
#define ROUTING_IRQ_SHORT    2u /**< Bytes of an IRQ descriptor without its information byte (ACPI 6.6 §6.4.2.1). */
#define ROUTING_IRQ_FULL     3u /**< With it. */
#define ROUTING_EXTENDED_SHORTEST                                                                                      \
    6u /**< Bytes of the shortest Extended Interrupt descriptor: flags, count and one interrupt (§6.4.3.6). */
#define ROUTING_EXTENDED_TABLE 2u          /**< Where its table of interrupts starts. */
#define ROUTING_SCRATCH_BYTES  8u          /**< What an Integer converted to bytes may take. */
#define ROUTING_FOLD_OFFSET    0x811C9DC5u /**< FNV-1a's offset basis. */
#define ROUTING_FOLD_PRIME     0x01000193u /**< FNV-1a's prime. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t RoutingStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t RoutingLoad_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t RoutingValue_t;
typedef AdvancedConfigurationAndPowerInterfaceInitialization_t RoutingCounts_t;
typedef AdvancedConfigurationAndPowerInterfaceInitializationMachine_t RoutingMachine_t;
typedef AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t RoutingTable_t;
typedef AdvancedConfigurationAndPowerInterfaceInterruptRoute_t Route_t;
typedef AdvancedConfigurationAndPowerInterfaceResourceInterrupt_t RoutingInterrupt_t;
typedef AdvancedConfigurationAndPowerInterfaceResource_t RoutingResource_t;

/**
 * @brief What reading an interrupt link found.
 */
typedef enum RoutingOutcome {
    ROUTING_OUTCOME_ROUTED = 0,   /**< Its current interrupt, which its _PRS lists. */
    ROUTING_OUTCOME_UNROUTED = 1, /**< No interrupt the kernel may take without choosing one through _SRS. */
    ROUTING_OUTCOME_FAILED = 2,   /**< Its _STA, _PRS or _CRS failed, gave no Buffer, or gave a template ACPICA
                                       refuses. */
    ROUTING_OUTCOME_DROPPED = 3,  /**< It came past the links one publication resolves. */
} RoutingOutcome_t;

/**
 * @brief An interrupt link read once, and what it gave.
 */
typedef struct RoutingLink {
    uint32_t node;                /**< The link's node. */
    RoutingOutcome_t outcome;     /**< What reading it found. */
    RoutingInterrupt_t interrupt; /**< Its interrupt, when routed. */
} RoutingLink_t;

/**
 * @brief The links one publication has read.
 */
typedef struct RoutingLinks {
    RoutingLink_t links[ROUTING_MAX_LINKS]; /**< In the order they were first named. */
    uint32_t count;                         /**< How many. */
} RoutingLinks_t;

/**
 * @brief The object the child of @p scope named @p name holds, or 0.
 */
static uint32_t routing_object(RoutingLoad_t *load, uint32_t scope, const char *name)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_find_object(
        &load->space, scope,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u));
}

/**
 * @brief Gives back what a value holds.
 */
static void routing_release(RoutingLoad_t *load, RoutingValue_t *value)
{
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, value);
}

/**
 * @brief How many elements a Package holds.
 */
static uint32_t routing_elements(const RoutingLoad_t *load, const RoutingValue_t *package)
{
    return advanced_configuration_and_power_interface_machine_language_object(&load->heap, package->handle)->length;
}

/**
 * @brief Evaluates the object named @p name of @p device into @p out_result, its failure counted.
 *
 * @return NAME_NOT_FOUND, nothing counted, when @p device has no such object; why the evaluation failed; OK.
 */
static RoutingStatus_t routing_evaluate(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t device, const char *name,
                                        RoutingValue_t *out_result)
{
    const uint32_t object = routing_object(load, device, name);
    RoutingStatus_t status;

    memset(out_result, 0, sizeof(*out_result));
    if (object == 0u)
        return ROUTING_STATUS(NAME_NOT_FOUND);
    status =
        advanced_configuration_and_power_interface_machine_language_evaluate_node(load, object, NULL, 0u, out_result);
    if (status != ROUTING_OK)
        advanced_configuration_and_power_interface_initialization_count_failure(counts, object, status);
    advanced_configuration_and_power_interface_initialization_settle(load, counts);
    return status;
}

/**
 * @brief Whether the _HID or the _CID of @p device names the device @p eisa_identifier, or @p text, stands for.
 */
static bool routing_identifies(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t device, uint32_t eisa_identifier,
                               const char *text)
{
    static const char *const names[] = {"_HID", "_CID"};
    bool named = false;

    for (uint32_t name = 0u; name < sizeof(names) / sizeof(names[0]) && !named; ++name)
    {
        RoutingValue_t identifier;

        if (routing_evaluate(load, counts, device, names[name], &identifier) == ROUTING_OK)
            named = advanced_configuration_and_power_interface_machine_language_identifies(load, &identifier,
                                                                                           eisa_identifier, text);
        routing_release(load, &identifier);
    }
    return named;
}

/**
 * @brief Whether @p device is a PCI root bridge: its _HID or its _CID names PNP0A03, as Linux v6.17 binds its PCI
 *        root driver (pci_root.c), a PCI Express root complex naming it in its _CID.
 */
static bool routing_is_root_bridge(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t device)
{
    return routing_identifies(load, counts, device, ROUTING_HOST_BRIDGE, "PNP0A03");
}

/**
 * @brief Whether _STA says @p device is present or functioning, as Linux attaches a device; one without _STA is,
 *        one whose _STA fails is not.
 */
static bool routing_present(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t device)
{
    uint64_t status = 0u;
    const bool read =
        advanced_configuration_and_power_interface_initialization_read_status(load, counts, device, &status);

    advanced_configuration_and_power_interface_initialization_settle(load, counts);
    return read && (status & (ROUTING_PRESENT | ROUTING_FUNCTIONING)) != 0u;
}

/**
 * @brief Whether @p device and every Device above it are present or functioning by their _STA, as Linux v6.17
 *        attaches no device below one that is neither (acpi_bus_attach).
 */
static bool routing_present_with_its_ancestors(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t device)
{
    for (uint32_t node = device; node != ROUTING_ROOT && node != 0u;
         node = advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node)->parent)
        if (advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node)->type ==
                ROUTING_NODE(DEVICE) &&
            !routing_present(load, counts, node))
            return false;
    return true;
}

/**
 * @brief Reads the Integer the object named @p name of @p bridge gives into @p out_value, 0 when it has none.
 *
 * @return false when it fails, counted, as Linux v6.17 then refuses the root bridge (acpi_pci_root_add).
 */
static bool routing_bridge_number(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t bridge, const char *name,
                                  uint64_t *out_value)
{
    const uint32_t object = routing_object(load, bridge, name);

    *out_value = 0u;
    if (object == 0u)
        return true;

    const bool read =
        advanced_configuration_and_power_interface_initialization_read_integer(load, counts, object, out_value);

    advanced_configuration_and_power_interface_initialization_settle(load, counts);
    return read;
}

/**
 * @brief Reads element @p element of @p package as an Integer into @p out_value.
 *
 * @return TYPE_MISMATCH when it is no Integer; why it could not be read; OK.
 */
static RoutingStatus_t routing_integer(RoutingLoad_t *load, const RoutingValue_t *package, uint32_t element,
                                       uint64_t *out_value)
{
    RoutingValue_t value;
    RoutingStatus_t status =
        advanced_configuration_and_power_interface_machine_language_package_element(load, package, element, &value);

    if (status == ROUTING_OK && value.kind != ROUTING_VALUE(INTEGER))
        status = ROUTING_MALFORMED;
    if (status == ROUTING_OK)
        *out_value = value.payload;
    routing_release(load, &value);
    return status;
}

/**
 * @brief Whether an IRQ or Extended Interrupt descriptor holds the bytes its form takes, as ACPICA validates it
 *        (AcpiUtValidateResource): an IRQ two or three, an Extended Interrupt at least six and its table of at least
 *        one interrupt; any other descriptor is not checked.
 */
static bool routing_descriptor_is_whole(const RoutingResource_t *resource)
{
    if (resource->name == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_INTERRUPT_REQUEST)
        return resource->length == ROUTING_IRQ_SHORT || resource->length == ROUTING_IRQ_FULL;
    if (resource->name != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_EXTENDED_INTERRUPT)
        return true;
    return resource->length >= ROUTING_EXTENDED_SHORTEST && resource->data[1] != 0u &&
           ROUTING_EXTENDED_TABLE + 4u * (uint32_t) resource->data[1] <= resource->length;
}

/**
 * @brief Whether a resource template is one ACPICA converts (AcpiUtWalkAmlResources): each descriptor within its
 *        bytes, each interrupt descriptor whole, and an End Tag of two bytes that closes it.
 */
static bool routing_template_is_whole(const uint8_t *bytes, uint32_t length)
{
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t iterator;
    RoutingResource_t resource;
    AdvancedConfigurationAndPowerInterfaceResourceStep_t step;

    advanced_configuration_and_power_interface_resources_begin(&iterator, bytes, length);
    while ((step = advanced_configuration_and_power_interface_resources_next(&iterator, &resource)) ==
           ROUTING_STEP(DESCRIPTOR))
        if (!routing_descriptor_is_whole(&resource))
            return false;
    return step == ROUTING_STEP(END) && iterator.offset + 1u < length;
}

/**
 * @brief The first IRQ or Extended Interrupt descriptor of a resource template that names an interrupt, in
 *        @p out_resource.
 *
 * @return false when the template holds none before its End Tag, or runs past its bytes first.
 */
static bool routing_first_interrupt_descriptor(const uint8_t *bytes, uint32_t length, RoutingResource_t *out_resource)
{
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t iterator;
    RoutingInterrupt_t first;

    advanced_configuration_and_power_interface_resources_begin(&iterator, bytes, length);
    while (advanced_configuration_and_power_interface_resources_next(&iterator, out_resource) ==
           ROUTING_STEP(DESCRIPTOR))
        if (advanced_configuration_and_power_interface_resources_interrupt(out_resource, 0u, &first))
            return true;
    return false;
}

/**
 * @brief Whether a descriptor lists @p number among its interrupts, 0 never counted, as Linux reads _PRS.
 */
static bool routing_lists(const RoutingResource_t *resource, uint32_t number)
{
    RoutingInterrupt_t interrupt;

    for (uint32_t index = 0u;
         advanced_configuration_and_power_interface_resources_interrupt(resource, index, &interrupt); ++index)
        if (interrupt.number == number && number != 0u)
            return true;
    return false;
}

/**
 * @brief Evaluates the resource template @p name of @p link, and finds its first interrupt descriptor.
 *
 * @param out_found Set when the template holds one, @p out_resource then pointing into @p out_result.
 * @return NAME_NOT_FOUND when @p link has no such object; TYPE_MISMATCH, counted, when it gives no Buffer or a
 *         template ACPICA refuses; why its evaluation failed; OK otherwise. The caller releases @p out_result.
 */
static RoutingStatus_t routing_template(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t link, const char *name,
                                        RoutingValue_t *out_result, RoutingResource_t *out_resource, bool *out_found)
{
    uint8_t scratch[ROUTING_SCRATCH_BYTES];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    RoutingStatus_t status = routing_evaluate(load, counts, link, name, out_result);

    *out_found = false;
    if (status == ROUTING_OK && out_result->kind != ROUTING_VALUE(BUFFER))
    {
        status = ROUTING_MALFORMED;
        advanced_configuration_and_power_interface_initialization_count_failure(
            counts, routing_object(load, link, name), status);
    }
    if (status == ROUTING_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_bytes(&load->heap, out_result, 64u,
                                                                                         scratch, &bytes, &length);
    if (status == ROUTING_OK && !routing_template_is_whole(bytes, length))
    {
        status = ROUTING_MALFORMED;
        advanced_configuration_and_power_interface_initialization_count_failure(
            counts, routing_object(load, link, name), status);
    }
    if (status == ROUTING_OK)
        *out_found = routing_first_interrupt_descriptor(bytes, length, out_resource);
    return status;
}

/**
 * @brief Reads interrupt link @p link as Linux v6.17 does: present or functioning by its _STA; then its _PRS and
 *        its _CRS, in that order, either absent giving no interrupt; its current interrupt the first one its _CRS
 *        names, kept when the first interrupt descriptor of its _PRS lists it, whose trigger and polarity it then
 *        takes.
 */
static RoutingOutcome_t routing_read_link(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t link,
                                          RoutingInterrupt_t *out_interrupt)
{
    RoutingValue_t current_template;
    RoutingValue_t possible_template;
    RoutingResource_t current;
    RoutingResource_t possible;
    bool has_current = false;
    bool has_possible = false;
    uint64_t status = 0u;
    RoutingOutcome_t outcome = ROUTING_OUTCOME_ROUTED;

    const bool read_status =
        advanced_configuration_and_power_interface_initialization_read_status(load, counts, link, &status);

    advanced_configuration_and_power_interface_initialization_settle(load, counts);
    if (!read_status)
        return ROUTING_OUTCOME_FAILED;
    if ((status & (ROUTING_PRESENT | ROUTING_FUNCTIONING)) == 0u)
        return ROUTING_OUTCOME_UNROUTED;

    const RoutingStatus_t read_possible =
        routing_template(load, counts, link, "_PRS", &possible_template, &possible, &has_possible);
    const RoutingStatus_t read_current =
        routing_template(load, counts, link, "_CRS", &current_template, &current, &has_current);

    if ((read_current != ROUTING_OK && read_current != ROUTING_STATUS(NAME_NOT_FOUND)) ||
        (read_possible != ROUTING_OK && read_possible != ROUTING_STATUS(NAME_NOT_FOUND)))
        outcome = ROUTING_OUTCOME_FAILED;
    else if (!has_current || !has_possible ||
             !advanced_configuration_and_power_interface_resources_interrupt(&current, 0u, out_interrupt) ||
             !routing_lists(&possible, out_interrupt->number))
        outcome = ROUTING_OUTCOME_UNROUTED;
    else
    {
        RoutingInterrupt_t flags;

        (void) advanced_configuration_and_power_interface_resources_interrupt(&possible, 0u, &flags);
        out_interrupt->level = flags.level;
        out_interrupt->active_low = flags.active_low;
    }
    routing_release(load, &current_template);
    routing_release(load, &possible_template);
    return outcome;
}

/**
 * @brief What link @p node gives: read the first time it is named, then taken from @p links; a link past the ones
 *        @p links holds is dropped.
 */
static RoutingOutcome_t routing_link(RoutingLoad_t *load, RoutingCounts_t *counts, RoutingLinks_t *links, uint32_t node,
                                     RoutingInterrupt_t *out_interrupt)
{
    for (uint32_t index = 0u; index < links->count; ++index)
    {
        if (links->links[index].node != node)
            continue;
        *out_interrupt = links->links[index].interrupt;
        return links->links[index].outcome;
    }
    if (links->count == ROUTING_MAX_LINKS)
        return ROUTING_OUTCOME_DROPPED;

    RoutingLink_t *link = &links->links[links->count++];

    memset(link, 0, sizeof(*link));
    link->node = node;
    link->outcome = routing_identifies(load, counts, node, ROUTING_LINK, "PNP0C0F") ?
                        routing_read_link(load, counts, node, &link->interrupt) :
                        ROUTING_OUTCOME_UNROUTED;
    *out_interrupt = link->interrupt;
    return link->outcome;
}

/**
 * @brief Reads the Source of an entry, element @p element: an Integer wires the pin, a name gives the link it names
 *        in @p out_link.
 *
 * @return false when the Source is neither, or its name resolves to no Device.
 */
static bool routing_source(RoutingLoad_t *load, const RoutingValue_t *entry, uint32_t element, uint32_t *out_link)
{
    RoutingValue_t source;
    const RoutingStatus_t read = advanced_configuration_and_power_interface_machine_language_package_element_node(
        load, entry, element, out_link);

    if (read == ROUTING_OK)
        return advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, *out_link)
                   ->type == ROUTING_NODE(DEVICE);
    *out_link = 0u;

    const bool wired = advanced_configuration_and_power_interface_machine_language_package_element(
                           load, entry, element, &source) == ROUTING_OK &&
                       source.kind == ROUTING_VALUE(INTEGER);

    routing_release(load, &source);
    return wired;
}

/**
 * @brief Reads one mapping package of a _PRT into @p out_route: its device, pin and Source, and the interrupt its
 *        SourceIndex or its link gives, none when the link gives none; counts what it found. A SourceIndex that is
 *        no Integer is read swapped with the Source, as ACPICA repairs it (AcpiNsRepair_PRT).
 *
 * @return true when the entry is published.
 */
static bool routing_read_entry(RoutingLoad_t *load, RoutingCounts_t *counts, RoutingTable_t *table,
                               RoutingLinks_t *links, const RoutingValue_t *entry, Route_t *out_route)
{
    uint64_t address = 0u;
    uint64_t pin = 0u;
    uint64_t index = 0u;
    uint32_t link = 0u;
    RoutingInterrupt_t interrupt = {.level = true, .active_low = true};
    const bool shaped =
        entry->kind == ROUTING_VALUE(PACKAGE) && routing_elements(load, entry) == ROUTING_ENTRY_ELEMENTS;
    const bool swapped = shaped && routing_integer(load, entry, 3u, &index) != ROUTING_OK;

    if (!shaped || routing_integer(load, entry, 0u, &address) != ROUTING_OK ||
        routing_integer(load, entry, 1u, &pin) != ROUTING_OK ||
        (swapped && routing_integer(load, entry, 2u, &index) != ROUTING_OK) ||
        !routing_source(load, entry, swapped ? 3u : 2u, &link) ||
        ((address >> ROUTING_DEVICE_SHIFT) & ROUTING_DEVICE_MASK) >= ROUTING_DEVICES || pin >= ROUTING_PINS)
    {
        ++table->entries_malformed;
        return false;
    }
    table->entries_repaired += swapped ? 1u : 0u;
    if (link != 0u)
    {
        ++table->links;
        const RoutingOutcome_t outcome =
            (index == 0u) ? routing_link(load, counts, links, link, &interrupt) : ROUTING_OUTCOME_UNROUTED;

        table->links_unrouted += (outcome == ROUTING_OUTCOME_UNROUTED) ? 1u : 0u;
        table->links_failed += (outcome == ROUTING_OUTCOME_FAILED) ? 1u : 0u;
        table->links_dropped += (outcome == ROUTING_OUTCOME_DROPPED) ? 1u : 0u;
        if (outcome != ROUTING_OUTCOME_ROUTED)
            interrupt = (RoutingInterrupt_t){.number = ROUTING_NO_INTERRUPT};
    }
    else
        interrupt.number = (uint32_t) index;
    out_route->interrupt = interrupt.number;
    out_route->link = link;
    out_route->device = (uint8_t) ((address >> ROUTING_DEVICE_SHIFT) & ROUTING_DEVICE_MASK);
    out_route->pin = (uint8_t) pin;
    out_route->level = interrupt.level ? 1u : 0u;
    out_route->active_low = interrupt.active_low ? 1u : 0u;
    return true;
}

/**
 * @brief Evaluates the _PRT of root bridge @p bridge, on segment @p segment and bus @p bus, and publishes each entry
 *        it holds, once the table is full only counting them.
 */
static void routing_read_bridge(RoutingLoad_t *load, RoutingCounts_t *counts, RoutingTable_t *table,
                                RoutingLinks_t *links, uint32_t bridge, uint16_t segment, uint8_t bus)
{
    RoutingValue_t routes;
    RoutingStatus_t status = routing_evaluate(load, counts, bridge, "_PRT", &routes);

    if (status == ROUTING_OK && routes.kind != ROUTING_VALUE(PACKAGE))
    {
        status = ROUTING_MALFORMED;
        advanced_configuration_and_power_interface_initialization_count_failure(
            counts, routing_object(load, bridge, "_PRT"), status);
    }
    if (status != ROUTING_OK)
        ++table->tables_failed;
    for (uint32_t element = 0u; status == ROUTING_OK && element < routing_elements(load, &routes); ++element)
    {
        RoutingValue_t entry;
        Route_t route = {.segment = segment, .bus = bus};

        ++table->entries;
        if (table->count == ROUTING_MAX_ROUTES)
        {
            ++table->entries_dropped;
            continue;
        }
        if (advanced_configuration_and_power_interface_machine_language_package_element(load, &routes, element,
                                                                                        &entry) != ROUTING_OK)
            ++table->entries_malformed;
        else if (routing_read_entry(load, counts, table, links, &entry, &route))
            table->routes[table->count++] = route;
        routing_release(load, &entry);
    }
    routing_release(load, &routes);
}

/**
 * @brief Folds the @p width low bytes of @p value, little-endian, into @p fold with FNV-1a.
 */
static uint32_t routing_fold_unsigned(uint32_t fold, uint64_t value, uint32_t width)
{
    for (uint32_t byte = 0u; byte < width; ++byte)
        fold = (fold ^ (uint8_t) (value >> (8u * byte))) * ROUTING_FOLD_PRIME;
    return fold;
}

void advanced_configuration_and_power_interface_interrupt_routing_publish(RoutingLoad_t *load,
                                                                          const RoutingMachine_t *machine,
                                                                          RoutingCounts_t *initialization,
                                                                          RoutingTable_t *out_table)
{
    RoutingLinks_t links;

    links.count = 0u;
    memset(out_table, 0, offsetof(RoutingTable_t, routes));
    out_table->model = (machine->interrupt_model == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_APIC &&
                        initialization->pic_mode_failed != 0u) ?
                           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_UNKNOWN :
                           machine->interrupt_model;
    for (uint32_t index = advanced_configuration_and_power_interface_machine_language_namespace_next(
             &load->space, ROUTING_ROOT, true);
         index != 0u;
         index = advanced_configuration_and_power_interface_machine_language_namespace_next(&load->space, index, true))
    {
        if (advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index)->type !=
                ROUTING_NODE(DEVICE) ||
            !routing_is_root_bridge(load, initialization, index))
            continue;
        uint64_t segment = 0u;
        uint64_t bus = 0u;

        ++out_table->bridges;
        if (!routing_present_with_its_ancestors(load, initialization, index))
            ++out_table->bridges_absent;
        else if (!routing_bridge_number(load, initialization, index, "_SEG", &segment) ||
                 !routing_bridge_number(load, initialization, index, "_BBN", &bus))
            ++out_table->bridges_failed;
        else if (routing_object(load, index, "_PRT") == 0u)
            ++out_table->bridges_unrouted;
        else
            routing_read_bridge(load, initialization, out_table, &links, index, (uint16_t) segment, (uint8_t) bus);
    }
}

bool advanced_configuration_and_power_interface_interrupt_routing_find(const RoutingTable_t *table, uint16_t segment,
                                                                       uint8_t bus, uint8_t device, uint8_t pin,
                                                                       Route_t *out_route)
{
    for (uint32_t index = 0u; index < table->count; ++index)
    {
        const Route_t *route = &table->routes[index];

        if (route->segment != segment || route->bus != bus || route->device != device || route->pin != pin)
            continue;
        *out_route = *route;
        return route->interrupt != ROUTING_NO_INTERRUPT;
    }
    return false;
}

const char *advanced_configuration_and_power_interface_interrupt_routing_model_name(uint8_t model)
{
    if (model == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_PIC)
        return "pic";
    return (model == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_APIC) ? "apic" : "unknown";
}

uint32_t advanced_configuration_and_power_interface_interrupt_routing_fold(const RoutingTable_t *table)
{
    uint32_t fold = routing_fold_unsigned(ROUTING_FOLD_OFFSET, table->model, 1u);

    fold = routing_fold_unsigned(fold, table->count, 4u);
    for (uint32_t index = 0u; index < table->count; ++index)
    {
        const Route_t *route = &table->routes[index];

        fold = routing_fold_unsigned(fold, route->interrupt, 4u);
        fold = routing_fold_unsigned(fold, route->segment, 2u);
        fold = routing_fold_unsigned(fold, route->bus, 1u);
        fold = routing_fold_unsigned(fold, route->device, 1u);
        fold = routing_fold_unsigned(fold, route->pin, 1u);
        fold = routing_fold_unsigned(fold, route->level, 1u);
        fold = routing_fold_unsigned(fold, route->active_low, 1u);
    }
    return fold;
}
