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
#define ROUTING_MAX_BRIDGES    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_BRIDGES
#define ROUTING_MAX_DEPTH      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_DEPTH
#define ROUTING_MAX_ROOTS      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_ROOTS
#define ROUTING_MAX_CHILDREN   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_CHILDREN
#define ROUTING_MALFORMED      ROUTING_STATUS(TYPE_MISMATCH) /**< What a result of the wrong shape fails as. */
#define ROUTING_ENTRY_ELEMENTS 4u      /**< A mapping package: Address, Pin, Source, SourceIndex (ACPI 6.6 §6.2.14). */
#define ROUTING_DEVICE_SHIFT   16u     /**< Where the device number starts in an Address, as in an _ADR. */
#define ROUTING_DEVICE_MASK    0xFFFFu /**< Its bits, as Linux reads them. */
#define ROUTING_DEVICES        32u     /**< Device numbers on a bus. */
#define ROUTING_FUNCTION_MASK  0xFFFFu /**< Where an _ADR names the function. */
#define ROUTING_FUNCTIONS      8u      /**< Function numbers of a device. */
#define ROUTING_PINS           4u      /**< INTA to INTD. */
#define ROUTING_PRESENT        0x01u   /**< _STA bit 0: the device is present. */
#define ROUTING_FUNCTIONING    0x08u   /**< _STA bit 3: the device works. */
#define ROUTING_ENABLED        0x02u   /**< _STA bit 1: the device is enabled and decodes its resources. */
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
typedef AdvancedConfigurationAndPowerInterfaceInterruptRoutingBridge_t RoutingBridge_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t RoutingNode_t;
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
 * @brief How Linux v6.17 scores one of the Devices of a bridge whose _ADR names the same PCI function
 *        (find_child_checks, a bridge's children checked), lowest first.
 */
typedef enum RoutingScore {
    ROUTING_SCORE_UNSCORED = 0, /**< Not scored yet. */
    ROUTING_SCORE_REFUSED = 1,  /**< Without children, or its _STA fails or says it is not enabled. */
    ROUTING_SCORE_LOW = 2,      /**< Without _STA, or enabled with a _HID. */
    ROUTING_SCORE_MIDDLE = 3,   /**< Without _STA, and a video device as Linux tells one (acpi_is_video_device). */
    ROUTING_SCORE_HIGH = 4,     /**< Enabled by its _STA, without a _HID: the walk ends at the first one. */
} RoutingScore_t;

/**
 * @brief A root bridge whose _PRT the first pass read, whose bridges below the second pass walks.
 */
typedef struct RoutingRoot {
    uint32_t node;    /**< The root bridge. */
    uint16_t segment; /**< Its _SEG. */
    uint8_t bus;      /**< Its _BBN. */
} RoutingRoot_t;

/**
 * @brief A Device of a bridge whose _ADR names a PCI function.
 */
typedef struct RoutingChild {
    uint32_t node;    /**< The Device. */
    uint32_t address; /**< Its _ADR: the device in bits 16 to 20, the function in bits 0 to 2. */
} RoutingChild_t;

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

static RoutingChild_t routing_children[ROUTING_MAX_CHILDREN]; /**< The Devices of the bridge being read whose _ADR
                                                                   names a PCI function, in declaration order. */

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
 * @brief Evaluates the _PRT of Device @p device, a root bridge on segment @p segment and bus @p bus or bridge
 *        @p bridge below it, and publishes each entry it holds, once the table is full only counting them.
 */
static void routing_read_table(RoutingLoad_t *load, RoutingCounts_t *counts, RoutingTable_t *table,
                               RoutingLinks_t *links, uint32_t device, uint16_t segment, uint8_t bus, uint16_t bridge)
{
    RoutingValue_t routes;
    RoutingStatus_t status = routing_evaluate(load, counts, device, "_PRT", &routes);

    if (status == ROUTING_OK && routes.kind != ROUTING_VALUE(PACKAGE))
    {
        status = ROUTING_MALFORMED;
        advanced_configuration_and_power_interface_initialization_count_failure(
            counts, routing_object(load, device, "_PRT"), status);
    }
    if (status != ROUTING_OK)
        ++table->tables_failed;
    for (uint32_t element = 0u; status == ROUTING_OK && element < routing_elements(load, &routes); ++element)
    {
        RoutingValue_t entry;
        Route_t route = {.segment = segment, .bridge = bridge, .bus = bus};

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
 * @brief The node at @p index.
 */
static const RoutingNode_t *routing_node(const RoutingLoad_t *load, uint32_t index)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);
}

/**
 * @brief Reads into @p out_address the PCI function the _ADR of @p device names, as Linux v6.17 matches a bridge's
 *        companion: a device below 32 in bits 16 to 31, a function below 8 in bits 0 to 15, nothing above them.
 *
 * @return false when @p device has no _ADR, its evaluation fails, counted, or it names no PCI function.
 */
static bool routing_function_address(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t device,
                                     uint32_t *out_address)
{
    const uint32_t object = routing_object(load, device, "_ADR");
    uint64_t address = 0u;

    if (object == 0u)
        return false;

    const bool read =
        advanced_configuration_and_power_interface_initialization_read_integer(load, counts, object, &address);

    advanced_configuration_and_power_interface_initialization_settle(load, counts);
    if (!read || (address >> ROUTING_DEVICE_SHIFT) >= ROUTING_DEVICES ||
        (address & ROUTING_FUNCTION_MASK) >= ROUTING_FUNCTIONS)
        return false;
    *out_address = (uint32_t) address;
    return true;
}

/**
 * @brief Whether @p device has a child Linux v6.17 gives a device object of its own (acpi_dev_has_children): a
 *        Device, a Processor, a ThermalZone or a PowerResource.
 */
static bool routing_has_children(const RoutingLoad_t *load, uint32_t device)
{
    for (uint32_t child = routing_node(load, device)->first_child; child != 0u;
         child = routing_node(load, child)->next_sibling)
    {
        const uint8_t type = routing_node(load, child)->type;

        if (type == ROUTING_NODE(DEVICE) || type == ROUTING_NODE(PROCESSOR) || type == ROUTING_NODE(THERMAL_ZONE) ||
            type == ROUTING_NODE(POWER_RESOURCE))
            return true;
    }
    return false;
}

/**
 * @brief Whether @p device is a video device as Linux v6.17 tells one (acpi_is_video_device): it has a _DOD, a _DOS
 *        or a _ROM, or a _VPO, a _GPD and a _SPD.
 */
static bool routing_is_video_device(RoutingLoad_t *load, uint32_t device)
{
    return routing_object(load, device, "_DOD") != 0u || routing_object(load, device, "_DOS") != 0u ||
           routing_object(load, device, "_ROM") != 0u ||
           (routing_object(load, device, "_VPO") != 0u && routing_object(load, device, "_GPD") != 0u &&
            routing_object(load, device, "_SPD") != 0u);
}

/**
 * @brief Whether @p device has a _HID ACPICA reads (AcpiUtExecute_HID): one that gives an Integer or a String.
 */
static bool routing_has_hardware_identifier(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t device)
{
    RoutingValue_t identifier;
    const bool valid = routing_evaluate(load, counts, device, "_HID", &identifier) == ROUTING_OK &&
                       (identifier.kind == ROUTING_VALUE(INTEGER) || identifier.kind == ROUTING_VALUE(STRING));

    routing_release(load, &identifier);
    return valid;
}

/**
 * @brief Scores @p device as Linux v6.17 scores one of the Devices of a bridge that name the same PCI function
 *        (find_child_checks, its children checked): refused without children; without _STA, or with one whose
 *        evaluation does not find a name, middle for a video device, low otherwise; refused when its _STA fails
 *        otherwise or says it is not enabled; low with a _HID, high without.
 */
static RoutingScore_t routing_score(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t device)
{
    uint64_t status = 0u;
    RoutingStatus_t read = ROUTING_STATUS(NAME_NOT_FOUND);

    if (!routing_has_children(load, device))
        return ROUTING_SCORE_REFUSED;
    if (routing_object(load, device, "_STA") != 0u)
    {
        read = advanced_configuration_and_power_interface_initialization_evaluate_status(load, counts, device, &status);
        advanced_configuration_and_power_interface_initialization_settle(load, counts);
    }
    if (read == ROUTING_STATUS(NAME_NOT_FOUND))
        return routing_is_video_device(load, device) ? ROUTING_SCORE_MIDDLE : ROUTING_SCORE_LOW;
    if (read != ROUTING_OK || (status & ROUTING_ENABLED) == 0u)
        return ROUTING_SCORE_REFUSED;
    return routing_has_hardware_identifier(load, counts, device) ? ROUTING_SCORE_LOW : ROUTING_SCORE_HIGH;
}

/**
 * @brief The Device Linux v6.17 takes among those routing_children holds from @p first on whose _ADR names the same
 *        function, as acpi_find_child_device walks them: the first one alone; among several, each scored once the
 *        walk reaches a second, the first one scored best, the walk ending at the first scored high.
 */
static uint32_t routing_choose(RoutingLoad_t *load, RoutingCounts_t *counts, uint32_t first, uint32_t count)
{
    uint32_t chosen = routing_children[first].node;
    RoutingScore_t best = ROUTING_SCORE_UNSCORED;

    for (uint32_t index = first + 1u; index < count && best != ROUTING_SCORE_HIGH; ++index)
    {
        if (routing_children[index].address != routing_children[first].address)
            continue;
        if (best == ROUTING_SCORE_UNSCORED)
        {
            best = routing_score(load, counts, chosen);
            if (best == ROUTING_SCORE_HIGH)
                break;
        }

        const RoutingScore_t score = routing_score(load, counts, routing_children[index].node);

        if (score > best)
        {
            chosen = routing_children[index].node;
            best = score;
        }
    }
    return chosen;
}

/**
 * @brief Whether a child routing_children holds before @p index names its function: that one decided it.
 */
static bool routing_address_taken(uint32_t index)
{
    for (uint32_t earlier = 0u; earlier < index; ++earlier)
        if (routing_children[earlier].address == routing_children[index].address)
            return true;
    return false;
}

/**
 * @brief Whether @p device, or a node below it, has a child named _PRT whose parent is a Device: only then may a
 *        bridge it stands for route anything.
 */
static bool routing_holds_a_table(const RoutingLoad_t *load, uint32_t device)
{
    const uint32_t name =
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "_PRT", 0u);
    const RoutingNode_t *nodes = load->space.nodes;
    uint32_t node = nodes[device].first_child;

    while (node != 0u)
    {
        const RoutingNode_t *current = &nodes[node];

        if (current->name == name && nodes[current->parent].type == ROUTING_NODE(DEVICE))
            return true;
        if (current->first_child != 0u)
            node = current->first_child;
        else
        {
            while (node != device && nodes[node].next_sibling == 0u)
                node = nodes[node].parent;
            node = (node == device) ? 0u : nodes[node].next_sibling;
        }
    }
    return false;
}

/**
 * @brief Whether a Device child of @p parent holds a _PRT or sits above one that does: only then may a bridge right
 *        below @p parent route anything.
 */
static bool routing_tables_below(const RoutingLoad_t *load, uint32_t parent)
{
    for (uint32_t child = routing_node(load, parent)->first_child; child != 0u;
         child = routing_node(load, child)->next_sibling)
        if (routing_node(load, child)->type == ROUTING_NODE(DEVICE) && routing_holds_a_table(load, child))
            return true;
    return false;
}

/**
 * @brief Whether a child routing_children holds after @p index, among the first @p count, names its function.
 */
static bool routing_address_shared(uint32_t index, uint32_t count)
{
    for (uint32_t later = index + 1u; later < count; ++later)
        if (routing_children[later].address == routing_children[index].address)
            return true;
    return false;
}

/**
 * @brief The first child routing_children holds from @p first on, among the first @p count, that names the function
 *        of the one at @p first and holds a _PRT or sits above one that does; @p count when none does.
 */
static uint32_t routing_first_holder(const RoutingLoad_t *load, uint32_t first, uint32_t count)
{
    for (uint32_t index = first; index < count; ++index)
        if (routing_children[index].address == routing_children[first].address &&
            routing_holds_a_table(load, routing_children[index].node))
            return index;
    return count;
}

/**
 * @brief Publishes the bridges right below @p parent, the root bridge on segment @p segment and bus @p bus when
 *        @p above is 0, bridge @p above below it otherwise, @p depth bridges below the root bridge, then reads each
 *        one's _PRT and the bridges below it, depth first.
 *
 * @details Each Device child whose _ADR names a PCI function stands for the PCI-to-PCI bridge Linux v6.17 would give
 *          it as its companion (acpi_pci_find_companion): of the children that name the same function, the one
 *          routing_choose takes; never, right below the root bridge, one whose _ADR names function 0 of device 0 and
 *          that has a _HID. It is published when it holds a _PRT or sits above one that does; one past the bridges a
 *          boot publishes, deeper than ROUTING_MAX_DEPTH, or past the ROUTING_MAX_CHILDREN children compared, is
 *          counted as dropped. No _ADR is read when no Device child holds a _PRT or sits above one that does, and the
 *          children that name one function are scored only when one of them does: no bridge would be published.
 */
static void routing_read_bridges_below(RoutingLoad_t *load, RoutingCounts_t *counts, RoutingTable_t *table,
                                       RoutingLinks_t *links, uint32_t parent, uint16_t above, uint16_t segment,
                                       uint8_t bus, uint32_t depth)
{
    const uint32_t first = table->bridges_below;
    uint32_t count = 0u;

    if (!routing_tables_below(load, parent))
        return;
    for (uint32_t child = routing_node(load, parent)->first_child; child != 0u;
         child = routing_node(load, child)->next_sibling)
    {
        uint32_t address = 0u;

        if (routing_node(load, child)->type != ROUTING_NODE(DEVICE) ||
            !routing_function_address(load, counts, child, &address))
            continue;
        if (count == ROUTING_MAX_CHILDREN)
            ++table->bridges_below_dropped;
        else
            routing_children[count++] = (RoutingChild_t){.node = child, .address = address};
    }
    if (count > table->children_peak)
        table->children_peak = count;
    for (uint32_t index = 0u; index < count; ++index)
    {
        if (routing_address_taken(index))
            continue;
        if (routing_address_shared(index, count))
            ++table->addresses_shared;

        const uint32_t holder = routing_first_holder(load, index, count);

        if (holder == count)
            continue;

        const uint32_t chosen = routing_choose(load, counts, index, count);

        if ((chosen != routing_children[holder].node && !routing_holds_a_table(load, chosen)) ||
            (above == 0u && routing_children[index].address == 0u &&
             routing_has_hardware_identifier(load, counts, chosen)))
            continue;
        if (depth > ROUTING_MAX_DEPTH || table->bridges_below == ROUTING_MAX_BRIDGES)
        {
            ++table->bridges_below_dropped;
            continue;
        }
        table->below[table->bridges_below++] = (RoutingBridge_t){.node = chosen,
                                                                 .address = routing_children[index].address,
                                                                 .segment = segment,
                                                                 .above = above,
                                                                 .bus = bus};
        if (depth > table->depth_peak)
            table->depth_peak = depth;
    }

    const uint32_t last = table->bridges_below;

    for (uint32_t index = first; index < last; ++index)
    {
        const uint32_t node = table->below[index].node;

        if (routing_object(load, node, "_PRT") != 0u)
            routing_read_table(load, counts, table, links, node, segment, bus, (uint16_t) (index + 1u));
        routing_read_bridges_below(load, counts, table, links, node, (uint16_t) (index + 1u), segment, bus, depth + 1u);
    }
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

/**
 * @brief Folds the bridges from a route's root bridge down to bridge @p bridge into @p fold: their count as one byte,
 *        then each one's address as four, the highest first.
 */
static uint32_t routing_fold_bridges(uint32_t fold, const RoutingTable_t *table, uint16_t bridge)
{
    uint32_t addresses[ROUTING_MAX_DEPTH];
    uint32_t depth = 0u;

    for (uint16_t index = bridge; index != 0u && depth < ROUTING_MAX_DEPTH; index = table->below[index - 1u].above)
        addresses[depth++] = table->below[index - 1u].address;
    fold = routing_fold_unsigned(fold, depth, 1u);
    while (depth != 0u)
        fold = routing_fold_unsigned(fold, addresses[--depth], 4u);
    return fold;
}

void advanced_configuration_and_power_interface_interrupt_routing_publish(RoutingLoad_t *load,
                                                                          const RoutingMachine_t *machine,
                                                                          RoutingCounts_t *initialization,
                                                                          RoutingTable_t *out_table)
{
    RoutingLinks_t links;
    RoutingRoot_t roots[ROUTING_MAX_ROOTS];
    uint32_t root_count = 0u;

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
        else
        {
            if (routing_object(load, index, "_PRT") == 0u)
                ++out_table->bridges_unrouted;
            else
                routing_read_table(load, initialization, out_table, &links, index, (uint16_t) segment, (uint8_t) bus,
                                   0u);
            if (root_count == ROUTING_MAX_ROOTS)
                ++out_table->bridges_unwalked;
            else
                roots[root_count++] =
                    (RoutingRoot_t){.node = index, .segment = (uint16_t) segment, .bus = (uint8_t) bus};
        }
    }
    for (uint32_t root = 0u; root < root_count; ++root)
        routing_read_bridges_below(load, initialization, out_table, &links, roots[root].node, 0u, roots[root].segment,
                                   roots[root].bus, 1u);
}

bool advanced_configuration_and_power_interface_interrupt_routing_find(const RoutingTable_t *table, uint16_t segment,
                                                                       uint8_t bus, uint8_t device, uint8_t pin,
                                                                       Route_t *out_route)
{
    for (uint32_t index = 0u; index < table->count; ++index)
    {
        const Route_t *route = &table->routes[index];

        if (route->bridge != 0u || route->segment != segment || route->bus != bus || route->device != device ||
            route->pin != pin)
            continue;
        *out_route = *route;
        return route->interrupt != ROUTING_NO_INTERRUPT;
    }
    return false;
}

bool advanced_configuration_and_power_interface_interrupt_routing_leaves_the_8259(const RoutingTable_t *table)
{
    return table->model == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_PIC;
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
        fold = routing_fold_bridges(fold, table, route->bridge);
        fold = routing_fold_unsigned(fold, route->device, 1u);
        fold = routing_fold_unsigned(fold, route->pin, 1u);
        fold = routing_fold_unsigned(fold, route->level, 1u);
        fold = routing_fold_unsigned(fold, route->active_low, 1u);
    }
    return fold;
}
