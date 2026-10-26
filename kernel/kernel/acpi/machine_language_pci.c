#include <kernel/acpi/machine_language.h>

#define MACHINE_LANGUAGE_OK               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_STATUS(name)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define MACHINE_LANGUAGE_VALUE(kind)      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind
#define MACHINE_LANGUAGE_NODE(type)       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define MACHINE_LANGUAGE_FIELD_UNIT(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_##kind
#define MACHINE_LANGUAGE_ROOT             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT
#define MACHINE_LANGUAGE_PCI_CONFIG                                                                                    \
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PERIPHERAL_COMPONENT_INTERCONNECT_CONFIGURATION
#define MACHINE_LANGUAGE_PCI_REACHABLE    0x100u /**< Configuration bytes reachable before ECAM (#58). */
#define MACHINE_LANGUAGE_PCI_HEADER_TYPE  0x0Eu  /**< The header type register. */
#define MACHINE_LANGUAGE_PCI_LAYOUT       0x7Fu  /**< The header type's layout, without the multi-function bit. */
#define MACHINE_LANGUAGE_PCI_BRIDGE       0x01u  /**< The layout of a PCI-to-PCI bridge. */
#define MACHINE_LANGUAGE_PCI_CARDBUS      0x02u  /**< The layout of a CardBus bridge. */
#define MACHINE_LANGUAGE_PCI_ABSENT       0xFFu  /**< What the header type of an absent function reads as. */
#define MACHINE_LANGUAGE_PCI_SECONDARY    0x19u  /**< A bridge's secondary bus number register. */
#define MACHINE_LANGUAGE_PCI_HEADER       0x40u  /**< The bytes of the predefined header. */
#define MACHINE_LANGUAGE_PCI_SUBSYSTEM    0x2Cu  /**< The subsystem vendor ID, the first byte AML may write. */
#define MACHINE_LANGUAGE_PCI_ROM          0x30u  /**< The expansion ROM base address. */
#define MACHINE_LANGUAGE_PCI_LINE         0x3Cu  /**< The interrupt line, the first byte after the ROM AML may write. */
#define MACHINE_LANGUAGE_PCI_DEVICES      32u    /**< Devices on a bus. */
#define MACHINE_LANGUAGE_PCI_FUNCTIONS    8u     /**< Functions of a device. */
#define MACHINE_LANGUAGE_PCI_HOST_BRIDGE  0x030AD041u /**< EisaId ("PNP0A03"): a PCI host bridge. */
#define MACHINE_LANGUAGE_PCI_EXPRESS_ROOT 0x080AD041u /**< EisaId ("PNP0A08"): a PCI Express root complex. */
#define MACHINE_LANGUAGE_ATTACH_START     0u          /**< The attach has not begun. */
#define MACHINE_LANGUAGE_ATTACH_ROOT_HID  1u   /**< Reads the _HID of attach_node, which may be the root bridge. */
#define MACHINE_LANGUAGE_ATTACH_ROOT_CID  2u   /**< Reads its _CID. */
#define MACHINE_LANGUAGE_ATTACH_SEGMENT   3u   /**< Reads the _SEG of attach_node, the root bridge. */
#define MACHINE_LANGUAGE_ATTACH_BUS       4u   /**< Reads its _BBN. */
#define MACHINE_LANGUAGE_ATTACH_ADDRESS   5u   /**< Reads the _ADR of attach_node, a device on the way down. */
#define MACHINE_LANGUAGE_ATTACH_ROOT_ADR  6u   /**< Reads the _ADR of attach_node, the root bridge itself. */
#define MACHINE_LANGUAGE_ATTACH_DONE      7u   /**< Attached. */
#define MACHINE_LANGUAGE_ATTACH_ABOVE     0x1u /**< pci_device holds a function found on the way down. */
#define MACHINE_LANGUAGE_ATTACH_NO_ROOT   0x2u /**< No root bridge is above the region. */
#define MACHINE_LANGUAGE_ATTACH_LOST      0x4u /**< Its function cannot be reached: all ones read, writes dropped. */
#define MACHINE_LANGUAGE_ATTACH_TYPE_ZERO 0x8u /**< Its function's header is of type 0. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t MachineLanguageLoad_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t MachineLanguageRegion_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t MachineLanguageFieldUnit_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t MachineLanguageValue_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t MachineLanguageNode_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t MachineLanguageHeader_t;
typedef AdvancedConfigurationAndPowerInterfaceHost_t MachineLanguageHost_t;

/**
 * @brief What reading the object an attach step names found.
 */
typedef enum MachineLanguageFound {
    MACHINE_LANGUAGE_FOUND_NOTHING = 0, /**< No such object, or one the attach does not read. */
    MACHINE_LANGUAGE_FOUND_VALUE = 1,   /**< A value, data or what a method returned. */
    MACHINE_LANGUAGE_FOUND_METHOD = 2,  /**< A method, whose result the attach waits for. */
} MachineLanguageFound_t;

/**
 * @brief What probing a function on the way to a region found.
 */
typedef enum MachineLanguageProbe {
    MACHINE_LANGUAGE_PROBE_FUNCTION = 0, /**< A function that is not a bridge: what lies below it belongs to it. */
    MACHINE_LANGUAGE_PROBE_BRIDGE = 1,   /**< A bridge with a secondary bus. */
    MACHINE_LANGUAGE_PROBE_LOST = 2,     /**< No function, or a bridge with no secondary bus. */
} MachineLanguageProbe_t;

/**
 * @brief The node at @p index, or NULL.
 */
static MachineLanguageNode_t *machine_language_pci_node(const MachineLanguageLoad_t *load, uint32_t index)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);
}

/**
 * @brief The object a node holds when it is of @p kind, past its header, or NULL.
 */
static void *machine_language_pci_object(const MachineLanguageLoad_t *load, uint32_t index, uint8_t kind)
{
    const MachineLanguageNode_t *node = machine_language_pci_node(load, index);

    if (node == NULL || node->value.kind != kind)
        return NULL;
    return advanced_configuration_and_power_interface_machine_language_object(&load->heap, node->value.handle) + 1;
}

/**
 * @brief The parent of a node, or 0 for the root.
 */
static uint32_t machine_language_pci_parent(const MachineLanguageLoad_t *load, uint32_t index)
{
    const MachineLanguageNode_t *node = machine_language_pci_node(load, index);

    return (node == NULL || index == MACHINE_LANGUAGE_ROOT) ? 0u : node->parent;
}

/**
 * @brief The closest Device at or above a node, or 0.
 */
static uint32_t machine_language_pci_device_of(const MachineLanguageLoad_t *load, uint32_t index)
{
    while (index != 0u && machine_language_pci_node(load, index)->type != MACHINE_LANGUAGE_NODE(DEVICE))
        index = machine_language_pci_parent(load, index);
    return index;
}

/**
 * @brief The child of @p ancestor on the way down to @p descendant, or 0 when @p descendant is not
 *        below it.
 */
static uint32_t machine_language_pci_step_toward(const MachineLanguageLoad_t *load, uint32_t ancestor,
                                                 uint32_t descendant)
{
    while (descendant != 0u && machine_language_pci_parent(load, descendant) != ancestor)
        descendant = machine_language_pci_parent(load, descendant);
    return descendant;
}

/**
 * @brief Whether a String holds @p identifier, seven characters, whole.
 */
static bool machine_language_pci_string_is(const MachineLanguageLoad_t *load, uint32_t handle, const char *identifier)
{
    const MachineLanguageHeader_t *header =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, handle);
    const uint8_t *bytes = (const uint8_t *) (header + 1);

    if (header == NULL || header->length < 7u || (header->length > 7u && bytes[7] != 0u))
        return false;
    for (uint32_t index = 0u; index < 7u; ++index)
        if (bytes[index] != (uint8_t) identifier[index])
            return false;
    return true;
}

/**
 * @brief Whether a value names a PCI root bridge: PNP0A03 or PNP0A08, as an EisaId or as a String.
 */
static bool machine_language_pci_names_root(const MachineLanguageLoad_t *load, const MachineLanguageValue_t *value)
{
    if (value->kind == MACHINE_LANGUAGE_VALUE(INTEGER))
        return value->payload == MACHINE_LANGUAGE_PCI_HOST_BRIDGE ||
               value->payload == MACHINE_LANGUAGE_PCI_EXPRESS_ROOT;
    return value->kind == MACHINE_LANGUAGE_VALUE(STRING) &&
           (machine_language_pci_string_is(load, value->handle, "PNP0A03") ||
            machine_language_pci_string_is(load, value->handle, "PNP0A08"));
}

/**
 * @brief Whether a _CID names a PCI root bridge: itself, or one element of its Package.
 */
static bool machine_language_pci_lists_root(const MachineLanguageLoad_t *load, const MachineLanguageValue_t *value)
{
    if (value->kind != MACHINE_LANGUAGE_VALUE(PACKAGE))
        return machine_language_pci_names_root(load, value);

    const MachineLanguageHeader_t *header =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, value->handle);
    const MachineLanguageValue_t *elements = (const MachineLanguageValue_t *) (header + 1);

    for (uint32_t index = 0u; header != NULL && index < header->length; ++index)
        if (machine_language_pci_names_root(load, &elements[index]))
            return true;
    return false;
}

/**
 * @brief The NameSeg the step @p attach reads.
 */
static uint32_t machine_language_pci_step_name(uint8_t attach)
{
    static const char names[][5] = {"____", "_HID", "_CID", "_SEG", "_BBN", "_ADR", "_ADR"};

    return advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) names[attach],
                                                                                    0u);
}

/**
 * @brief Reads the object the region's step names: @p result when a method gave it, else the child of
 *        attach_node, a value at once or a method to evaluate without arguments. A device the walk
 *        down to the region reaches must be a Device; anything else, an External or a method that
 *        takes arguments is nothing.
 */
static MachineLanguageFound_t machine_language_pci_read_step(MachineLanguageLoad_t *load,
                                                             const MachineLanguageRegion_t *region,
                                                             const MachineLanguageValue_t *result,
                                                             MachineLanguageValue_t *out_value, uint32_t *out_method)
{
    const MachineLanguageNode_t *at = machine_language_pci_node(load, region->attach_node);
    uint32_t child = 0u;

    if (result != NULL)
    {
        *out_value = *result;
        return MACHINE_LANGUAGE_FOUND_VALUE;
    }
    if (at == NULL || (region->attach == MACHINE_LANGUAGE_ATTACH_ADDRESS && at->type != MACHINE_LANGUAGE_NODE(DEVICE)))
        return MACHINE_LANGUAGE_FOUND_NOTHING;
    child = advanced_configuration_and_power_interface_machine_language_namespace_find_object(
        &load->space, region->attach_node, machine_language_pci_step_name(region->attach));

    const MachineLanguageNode_t *node = machine_language_pci_node(load, child);

    if (node == NULL)
        return MACHINE_LANGUAGE_FOUND_NOTHING;
    if (node->type == MACHINE_LANGUAGE_NODE(METHOD))
    {
        if ((node->value.auxiliary & 0x7u) != 0u)
            return MACHINE_LANGUAGE_FOUND_NOTHING;
        *out_method = child;
        return MACHINE_LANGUAGE_FOUND_METHOD;
    }
    *out_value = node->value;
    return MACHINE_LANGUAGE_FOUND_VALUE;
}

/**
 * @brief The device and function an _ADR gives, as the low byte of pci_device, or false when it is
 *        not an Integer or names no function a bus holds.
 */
static bool machine_language_pci_function_of(const MachineLanguageValue_t *value, uint32_t *out_function)
{
    const uint64_t device = (value->payload >> 16u) & 0xFFFFu;
    const uint64_t function = value->payload & 0xFFFFu;

    if (value->kind != MACHINE_LANGUAGE_VALUE(INTEGER) || device >= MACHINE_LANGUAGE_PCI_DEVICES ||
        function >= MACHINE_LANGUAGE_PCI_FUNCTIONS)
        return false;
    *out_function = (uint32_t) ((device << 3u) | function);
    return true;
}

/**
 * @brief One byte of the configuration space of the function pci_device names.
 */
static uint8_t machine_language_pci_byte(const MachineLanguageLoad_t *load, uint32_t pci_device, uint16_t offset)
{
    const MachineLanguageHost_t *host = load->space.host;

    return (uint8_t) host->pci_read(host->context, (uint16_t) (pci_device >> 16u), (uint8_t) (pci_device >> 8u),
                                    (uint8_t) ((pci_device >> 3u) & 0x1Fu), (uint8_t) (pci_device & 0x7u), offset, 1u);
}

/**
 * @brief Probes the function pci_device names, on the way down to a region: a bridge gives its
 *        secondary bus.
 */
static MachineLanguageProbe_t machine_language_pci_probe(const MachineLanguageLoad_t *load, uint32_t pci_device,
                                                         uint8_t *out_bus)
{
    const uint8_t header = machine_language_pci_byte(load, pci_device, MACHINE_LANGUAGE_PCI_HEADER_TYPE);
    const uint8_t layout = header & MACHINE_LANGUAGE_PCI_LAYOUT;

    if (header == MACHINE_LANGUAGE_PCI_ABSENT)
        return MACHINE_LANGUAGE_PROBE_LOST;
    if (layout != MACHINE_LANGUAGE_PCI_BRIDGE && layout != MACHINE_LANGUAGE_PCI_CARDBUS)
        return MACHINE_LANGUAGE_PROBE_FUNCTION;
    *out_bus = machine_language_pci_byte(load, pci_device, MACHINE_LANGUAGE_PCI_SECONDARY);
    return (*out_bus == 0u) ? MACHINE_LANGUAGE_PROBE_LOST : MACHINE_LANGUAGE_PROBE_BRIDGE;
}

/**
 * @brief Ends the attach: the function's header type is read once, for its protection, unless it is
 *        lost or needs ECAM.
 */
static void machine_language_pci_finish(MachineLanguageLoad_t *load, MachineLanguageRegion_t *region, uint8_t flags)
{
    region->attach = MACHINE_LANGUAGE_ATTACH_DONE;
    region->attach_flags |= flags;
    region->attach_node = 0u;
    ++load->regions.pci_attachments;
    if ((region->attach_flags & MACHINE_LANGUAGE_ATTACH_LOST) == 0u && (region->pci_device >> 16u) == 0u &&
        (machine_language_pci_byte(load, region->pci_device, MACHINE_LANGUAGE_PCI_HEADER_TYPE) &
         MACHINE_LANGUAGE_PCI_LAYOUT) == 0u)
        region->attach_flags |= MACHINE_LANGUAGE_ATTACH_TYPE_ZERO;
}

/**
 * @brief Starts the walk down from the root bridge to the region's device: its first device, or the
 *        root's own _ADR when the region lies in the root itself.
 */
static void machine_language_pci_walk_down(MachineLanguageLoad_t *load, uint32_t region_node,
                                           MachineLanguageRegion_t *region)
{
    const uint32_t device = machine_language_pci_device_of(load, region_node);
    const uint32_t first = machine_language_pci_step_toward(load, region->attach_node, device);

    if (first == 0u)
        region->attach = MACHINE_LANGUAGE_ATTACH_ROOT_ADR;
    else
    {
        region->attach_node = first;
        region->attach = MACHINE_LANGUAGE_ATTACH_ADDRESS;
    }
}

/**
 * @brief Ends the walk down at the first node that is not a device with an _ADR: the region belongs to
 *        the last function found; with none, to the root bridge's own _ADR; with no root bridge, to
 *        nothing.
 */
static void machine_language_pci_stop_walk(MachineLanguageLoad_t *load, MachineLanguageRegion_t *region)
{
    if ((region->attach_flags & MACHINE_LANGUAGE_ATTACH_ABOVE) != 0u)
        machine_language_pci_finish(load, region, 0u);
    else if ((region->attach_flags & MACHINE_LANGUAGE_ATTACH_NO_ROOT) != 0u)
        machine_language_pci_finish(load, region, MACHINE_LANGUAGE_ATTACH_LOST);
    else
    {
        region->attach_node = machine_language_pci_parent(load, region->attach_node);
        region->attach = MACHINE_LANGUAGE_ATTACH_ROOT_ADR;
    }
}

/**
 * @brief Takes the next device on the way down, @p address its _ADR: the function found before it is
 *        probed first, and a bridge moves the walk to its secondary bus, while any other function
 *        keeps the region, whatever the _ADR below it holds; past a bridge, an _ADR that names no
 *        device and function a bus holds loses the region.
 */
static void machine_language_pci_descend(MachineLanguageLoad_t *load, uint32_t region_node,
                                         MachineLanguageRegion_t *region, const MachineLanguageValue_t *address)
{
    const uint32_t device = machine_language_pci_device_of(load, region_node);
    uint32_t function = 0u;
    uint8_t bus = 0u;

    if ((region->attach_flags & MACHINE_LANGUAGE_ATTACH_ABOVE) != 0u)
    {
        const MachineLanguageProbe_t probe = machine_language_pci_probe(load, region->pci_device, &bus);

        if (probe != MACHINE_LANGUAGE_PROBE_BRIDGE)
        {
            machine_language_pci_finish(load, region,
                                        (probe == MACHINE_LANGUAGE_PROBE_LOST) ? MACHINE_LANGUAGE_ATTACH_LOST : 0u);
            return;
        }
        region->pci_device = (region->pci_device & 0xFFFF0000u) | ((uint32_t) bus << 8u);
    }
    if (!machine_language_pci_function_of(address, &function))
    {
        machine_language_pci_finish(load, region, MACHINE_LANGUAGE_ATTACH_LOST);
        return;
    }
    region->pci_device = (region->pci_device & 0xFFFFFF00u) | function;
    region->attach_flags |= MACHINE_LANGUAGE_ATTACH_ABOVE;
    if (region->attach_node == device)
        machine_language_pci_finish(load, region, 0u);
    else
        region->attach_node = machine_language_pci_step_toward(load, region->attach_node, device);
}

/**
 * @brief Looks for the root bridge one ancestor higher, or, past the last, attaches the region with no
 *        root: segment 0, bus 0, and its device's _ADR.
 */
static void machine_language_pci_climb(MachineLanguageLoad_t *load, uint32_t region_node,
                                       MachineLanguageRegion_t *region)
{
    const uint32_t parent = machine_language_pci_parent(load, region->attach_node);
    const uint32_t device = machine_language_pci_device_of(load, region_node);

    if (parent != MACHINE_LANGUAGE_ROOT && parent != 0u)
    {
        region->attach_node = parent;
        region->attach = MACHINE_LANGUAGE_ATTACH_ROOT_HID;
        return;
    }
    region->attach_flags |= MACHINE_LANGUAGE_ATTACH_NO_ROOT;
    if (device == 0u)
    {
        machine_language_pci_finish(load, region, MACHINE_LANGUAGE_ATTACH_LOST);
        return;
    }
    region->attach_node = device;
    region->attach = MACHINE_LANGUAGE_ATTACH_ADDRESS;
}

/**
 * @brief Moves the attach past its step, given what the step's object held, or NULL when it is absent.
 */
static void machine_language_pci_advance(MachineLanguageLoad_t *load, uint32_t region_node,
                                         MachineLanguageRegion_t *region, const MachineLanguageValue_t *value)
{
    const bool integer = value != NULL && value->kind == MACHINE_LANGUAGE_VALUE(INTEGER);
    uint32_t function = 0u;

    switch (region->attach)
    {
    case MACHINE_LANGUAGE_ATTACH_ROOT_HID:
        region->attach = (value != NULL && machine_language_pci_names_root(load, value)) ?
                             MACHINE_LANGUAGE_ATTACH_SEGMENT :
                             MACHINE_LANGUAGE_ATTACH_ROOT_CID;
        break;
    case MACHINE_LANGUAGE_ATTACH_ROOT_CID:
        if (value != NULL && machine_language_pci_lists_root(load, value))
            region->attach = MACHINE_LANGUAGE_ATTACH_SEGMENT;
        else
            machine_language_pci_climb(load, region_node, region);
        break;
    case MACHINE_LANGUAGE_ATTACH_SEGMENT:
        region->pci_device = integer ? (uint32_t) (value->payload & 0xFFFFu) << 16u : 0u;
        region->attach = MACHINE_LANGUAGE_ATTACH_BUS;
        break;
    case MACHINE_LANGUAGE_ATTACH_BUS:
        region->pci_device |= integer ? (uint32_t) (value->payload & 0xFFu) << 8u : 0u;
        if ((region->pci_device >> 16u) != 0u)
            machine_language_pci_finish(load, region, 0u);
        else
            machine_language_pci_walk_down(load, region_node, region);
        break;
    case MACHINE_LANGUAGE_ATTACH_ADDRESS:
        if (!integer)
            machine_language_pci_stop_walk(load, region);
        else
            machine_language_pci_descend(load, region_node, region, value);
        break;
    default:
        if (value == NULL || !machine_language_pci_function_of(value, &function))
            machine_language_pci_finish(load, region, MACHINE_LANGUAGE_ATTACH_LOST);
        else
        {
            region->pci_device |= function;
            machine_language_pci_finish(load, region, MACHINE_LANGUAGE_ATTACH_ABOVE);
        }
        break;
    }
}

/**
 * @brief The PCI_Config region at a node that is not attached yet, or 0.
 */
static uint32_t machine_language_pci_unattached(const MachineLanguageLoad_t *load, uint32_t region_node)
{
    const MachineLanguageRegion_t *region =
        machine_language_pci_object(load, region_node, MACHINE_LANGUAGE_VALUE(REGION));

    return (region != NULL && region->space == MACHINE_LANGUAGE_PCI_CONFIG &&
            region->attach != MACHINE_LANGUAGE_ATTACH_DONE) ?
               region_node :
               0u;
}

/**
 * @brief The region of a unit of a Field that is a PCI_Config region not attached yet, or 0.
 */
static uint32_t machine_language_pci_register_unattached(const MachineLanguageLoad_t *load, uint32_t unit_node)
{
    const MachineLanguageFieldUnit_t *unit =
        machine_language_pci_object(load, unit_node, MACHINE_LANGUAGE_VALUE(FIELD_UNIT));

    return (unit != NULL && unit->kind == MACHINE_LANGUAGE_FIELD_UNIT(NORMAL)) ?
               machine_language_pci_unattached(load, unit->region) :
               0u;
}

/**
 * @brief Whether a write at @p offset of the predefined header is dropped: below the subsystem vendor
 *        ID and from the expansion ROM to the interrupt line of a type-0 header, and anywhere in the
 *        header of any other type. Only an access's first byte is looked at, as Windows does.
 */
static bool machine_language_pci_protects(const MachineLanguageRegion_t *region, uint64_t offset)
{
    if (offset >= MACHINE_LANGUAGE_PCI_HEADER)
        return false;
    if ((region->attach_flags & MACHINE_LANGUAGE_ATTACH_TYPE_ZERO) == 0u)
        return true;
    return offset < MACHINE_LANGUAGE_PCI_SUBSYSTEM ||
           (offset >= MACHINE_LANGUAGE_PCI_ROM && offset < MACHINE_LANGUAGE_PCI_LINE);
}

/**
 * @brief One access of 1, 2 or 4 bytes at an offset its width divides, a protected write dropped and
 *        counted.
 */
static void machine_language_pci_aligned(MachineLanguageLoad_t *load, const MachineLanguageRegion_t *region,
                                         uint16_t offset, uint8_t width, bool write, uint32_t *value)
{
    const MachineLanguageHost_t *host = load->space.host;
    const uint32_t pci_device = region->pci_device;
    const uint16_t segment = (uint16_t) (pci_device >> 16u);
    const uint8_t bus = (uint8_t) (pci_device >> 8u);
    const uint8_t device = (uint8_t) ((pci_device >> 3u) & 0x1Fu);
    const uint8_t function = (uint8_t) (pci_device & 0x7u);

    if (write && machine_language_pci_protects(region, offset))
        ++load->regions.pci_protected_writes;
    else if (write)
        host->pci_write(host->context, segment, bus, device, function, offset, width, *value);
    else
        *value = host->pci_read(host->context, segment, bus, device, function, offset, width);
}

MachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_attach(
    MachineLanguageLoad_t *load, uint32_t region_node, const MachineLanguageValue_t *result, uint32_t *out_method)
{
    MachineLanguageRegion_t *region = machine_language_pci_object(load, region_node, MACHINE_LANGUAGE_VALUE(REGION));

    *out_method = 0u;
    if (region == NULL || region->space != MACHINE_LANGUAGE_PCI_CONFIG)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (region->attach == MACHINE_LANGUAGE_ATTACH_START)
    {
        region->attach_flags = 0u;
        region->pci_device = 0u;
        region->attach_node = region_node;
        machine_language_pci_climb(load, region_node, region);
    }
    while (region->attach != MACHINE_LANGUAGE_ATTACH_DONE)
    {
        MachineLanguageValue_t value = {0};
        const MachineLanguageFound_t found = machine_language_pci_read_step(load, region, result, &value, out_method);

        result = NULL;
        if (found == MACHINE_LANGUAGE_FOUND_METHOD)
            return MACHINE_LANGUAGE_OK;
        machine_language_pci_advance(load, region_node, region,
                                     (found == MACHINE_LANGUAGE_FOUND_VALUE) ? &value : NULL);
    }
    return MACHINE_LANGUAGE_OK;
}

uint32_t
advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_unattached_region(
    const MachineLanguageLoad_t *load, uint32_t unit_node)
{
    const MachineLanguageFieldUnit_t *unit =
        machine_language_pci_object(load, unit_node, MACHINE_LANGUAGE_VALUE(FIELD_UNIT));
    uint32_t region = 0u;

    if (unit == NULL)
        return 0u;
    if (unit->kind == MACHINE_LANGUAGE_FIELD_UNIT(INDEX))
    {
        region = machine_language_pci_register_unattached(load, unit->region);
        return (region != 0u) ? region : machine_language_pci_register_unattached(load, unit->data_or_bank);
    }
    region = machine_language_pci_unattached(load, unit->region);
    if (region == 0u && unit->kind == MACHINE_LANGUAGE_FIELD_UNIT(BANK))
        region = machine_language_pci_register_unattached(load, unit->data_or_bank);
    return region;
}

MachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_access(
    MachineLanguageLoad_t *load, uint32_t region_node, uint64_t offset, uint8_t width, bool write, uint32_t *value)
{
    uint32_t method = 0u;
    MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_attach(
            load, region_node, NULL, &method);
    const MachineLanguageRegion_t *region =
        machine_language_pci_object(load, region_node, MACHINE_LANGUAGE_VALUE(REGION));

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (method != 0u)
        return MACHINE_LANGUAGE_STATUS(REGION_UNEVALUATED);
    if ((region->attach_flags & MACHINE_LANGUAGE_ATTACH_LOST) != 0u)
    {
        ++load->regions.pci_lost_accesses;
        if (!write)
            *value = (width >= 4u) ? 0xFFFFFFFFu : ((1u << (8u * width)) - 1u);
        return MACHINE_LANGUAGE_OK;
    }
    if ((region->pci_device >> 16u) != 0u || offset >= MACHINE_LANGUAGE_PCI_REACHABLE ||
        width > MACHINE_LANGUAGE_PCI_REACHABLE - offset)
    {
        ++load->regions.pci_ecam_accesses;
        return MACHINE_LANGUAGE_STATUS(ADDRESS_UNREACHABLE);
    }
    if (offset % width == 0u)
    {
        machine_language_pci_aligned(load, region, (uint16_t) offset, width, write, value);
        return MACHINE_LANGUAGE_OK;
    }
    ++load->regions.unaligned_accesses;

    uint32_t result = 0u;

    for (uint8_t byte = 0u; byte < width; ++byte)
    {
        uint32_t part = (*value >> (8u * byte)) & 0xFFu;

        machine_language_pci_aligned(load, region, (uint16_t) (offset + byte), 1u, write, &part);
        result |= (part & 0xFFu) << (8u * byte);
    }
    if (!write)
        *value = result;
    return MACHINE_LANGUAGE_OK;
}

uint32_t advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_registration(
    MachineLanguageLoad_t *load, uint32_t region_node)
{
    const MachineLanguageRegion_t *region =
        machine_language_pci_object(load, region_node, MACHINE_LANGUAGE_VALUE(REGION));

    if (region == NULL || region->space != MACHINE_LANGUAGE_PCI_CONFIG)
        return 0u;

    const uint32_t method = advanced_configuration_and_power_interface_machine_language_namespace_find_object(
        &load->space, machine_language_pci_parent(load, region_node),
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "_REG", 0u));
    const MachineLanguageNode_t *node = machine_language_pci_node(load, method);

    return (node != NULL && node->type == MACHINE_LANGUAGE_NODE(METHOD) && (node->value.auxiliary & 0x7u) == 2u) ?
               method :
               0u;
}

void advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_registered(
    MachineLanguageLoad_t *load)
{
    load->regions.registers_at_creation = true;
}
