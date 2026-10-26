#include <kernel/acpi/machine_language.h>

#include <string.h>

#define MACHINE_LANGUAGE_OK               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_STATUS(name)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define MACHINE_LANGUAGE_VALUE(kind)      ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind
#define MACHINE_LANGUAGE_FIELD_UNIT(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_##kind
#define MACHINE_LANGUAGE_MAPPING(kind)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAPPING_##kind
#define MACHINE_LANGUAGE_MEMORY(type)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_##type
#define MACHINE_LANGUAGE_PAGE_BYTES       4096u
#define MACHINE_LANGUAGE_SYSTEM_MEMORY    0x00u         /**< The RegionSpace byte of SystemMemory. */
#define MACHINE_LANGUAGE_SYSTEM_IO        0x01u         /**< The RegionSpace byte of SystemIO. */
#define MACHINE_LANGUAGE_PCI_CONFIG       0x02u         /**< The RegionSpace byte of PCI_Config. */
#define MACHINE_LANGUAGE_UPDATE_ONES      1u            /**< UpdateRule WriteAsOnes. */
#define MACHINE_LANGUAGE_UPDATE_ZEROS     2u            /**< UpdateRule WriteAsZeros. */
#define MACHINE_LANGUAGE_ACCESS_LAST      5u            /**< AccessType BufferAcc, the last one not reserved. */
#define MACHINE_LANGUAGE_REACHABLE_LAST   0xFFFFFFFFull /**< The last physical byte i686 reaches (§5.3). */
#define MACHINE_LANGUAGE_LOCK_PENDING     0x1u          /**< GlobalLock bit Pending: the other side wants the lock. */
#define MACHINE_LANGUAGE_LOCK_OWNED       0x2u          /**< GlobalLock bit Owned. */
#define MACHINE_LANGUAGE_LOCK_RELEASE     0x4u          /**< GBL_RLS, bit 2 of a PM1 control register. */
#define MACHINE_LANGUAGE_FACS_LOCK_OFFSET 16u           /**< Where the GlobalLock word lies in the FACS. */
#define MACHINE_LANGUAGE_FACS_MINIMUM     24u           /**< The shortest FACS that holds a GlobalLock word. */
#define MACHINE_LANGUAGE_FACS_ALIGNMENT                                                                                \
    64u /**< The alignment ACPI gives a FACS, which keeps its lock word in one page. */
#define MACHINE_LANGUAGE_CONTROL_WRITE_ONLY 0x2004u /**< SLP_EN and GBL_RLS: the write-only bits of PM1 control. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t MachineLanguageLoad_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t MachineLanguageRegions_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageMapping_t MachineLanguageMapping_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t MachineLanguageRegion_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t MachineLanguageFieldUnit_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t MachineLanguageValue_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t MachineLanguageNode_t;
typedef AdvancedConfigurationAndPowerInterfaceHost_t MachineLanguageHost_t;

/**
 * @brief Where a datum of a field lies: its first bit, and the bits of it the unit covers.
 */
typedef struct MachineLanguageDatum {
    uint64_t first_bit; /**< Its first bit, counted from the start of the field list. */
    uint32_t shift;     /**< The first bit of it the unit covers. */
    uint32_t count;     /**< How many bits of it the unit covers. */
} MachineLanguageDatum_t;

/**
 * @brief The bytes of one datum of @p access, an AccessType: AnyAcc and BufferAcc access bytes, as
 *        Windows does; a reserved type is refused before it gets here.
 */
static uint8_t machine_language_access_bytes(uint8_t access)
{
    switch (access)
    {
    case 2u: return 2u;
    case 3u: return 4u;
    case 4u: return 8u;
    default: return 1u;
    }
}

/**
 * @brief The low @p count bits set, for a count from 0 to 64.
 */
static uint64_t machine_language_low_bits(uint32_t count) { return (count >= 64u) ? ~0ull : ((1ull << count) - 1u); }

/**
 * @brief Writes the low @p count bits of @p bits into @p bytes, little-endian, from bit @p position.
 */
static void machine_language_put_bits(uint8_t *bytes, uint64_t position, uint64_t bits, uint32_t count)
{
    for (uint32_t bit = 0u; bit < count; ++bit)
    {
        const uint64_t at = position + bit;
        const uint8_t mask = (uint8_t) (1u << (at & 7u));

        bytes[at >> 3u] = (uint8_t) (((bits >> bit) & 1u) ? (bytes[at >> 3u] | mask) : (bytes[at >> 3u] & ~mask));
    }
}

/**
 * @brief Reads @p count bits of @p bytes, little-endian, from bit @p position; the bits past
 *        @p length bytes read as 0.
 */
static uint64_t machine_language_get_bits(const uint8_t *bytes, uint32_t length, uint64_t position, uint32_t count)
{
    uint64_t bits = 0u;

    for (uint32_t bit = 0u; bit < count; ++bit)
    {
        const uint64_t at = position + bit;

        if ((at >> 3u) < length && ((bytes[at >> 3u] >> (at & 7u)) & 1u) != 0u)
            bits |= 1ull << bit;
    }
    return bits;
}

/**
 * @brief The datum of @p datum_bits that starts at @p first_bit, and the bits of
 *        [@p bit_offset, @p end) it holds.
 */
static MachineLanguageDatum_t machine_language_datum(uint64_t first_bit, uint32_t datum_bits, uint64_t bit_offset,
                                                     uint64_t end)
{
    const uint64_t low = (bit_offset > first_bit) ? bit_offset : first_bit;
    const uint64_t high = (end < first_bit + datum_bits) ? end : first_bit + datum_bits;
    const MachineLanguageDatum_t datum = {first_bit, (uint32_t) (low - first_bit), (uint32_t) (high - low)};

    return datum;
}

/**
 * @brief The value a datum of @p datum_bits takes when the unit writes @p bits over part of it:
 *        what @p old holds elsewhere for Preserve, ones for WriteAsOnes, zeros for WriteAsZeros.
 */
static uint64_t machine_language_merge(uint8_t update, uint64_t old, uint64_t bits, const MachineLanguageDatum_t *datum,
                                       uint32_t datum_bits)
{
    const uint64_t mask = machine_language_low_bits(datum->count) << datum->shift;
    const uint64_t around = (update == MACHINE_LANGUAGE_UPDATE_ONES)  ? machine_language_low_bits(datum_bits) :
                            (update == MACHINE_LANGUAGE_UPDATE_ZEROS) ? 0u :
                                                                        old;

    return (around & ~mask) | ((bits << datum->shift) & mask);
}

/**
 * @brief The field unit a node holds, or NULL.
 */
static const MachineLanguageFieldUnit_t *machine_language_unit(const MachineLanguageLoad_t *load, uint32_t index)
{
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    if (node == NULL || node->value.kind != MACHINE_LANGUAGE_VALUE(FIELD_UNIT))
        return NULL;
    return (const MachineLanguageFieldUnit_t *) (advanced_configuration_and_power_interface_machine_language_object(
                                                     &load->heap, node->value.handle) +
                                                 1);
}

/**
 * @brief The region a node holds, or NULL.
 */
static const MachineLanguageRegion_t *machine_language_region(const MachineLanguageLoad_t *load, uint32_t index)
{
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    if (node == NULL || node->value.kind != MACHINE_LANGUAGE_VALUE(REGION))
        return NULL;
    return (const MachineLanguageRegion_t *) (advanced_configuration_and_power_interface_machine_language_object(
                                                  &load->heap, node->value.handle) +
                                              1);
}

/**
 * @brief Gives a mapping of the cache back to the host.
 */
static void machine_language_unmap(MachineLanguageLoad_t *load, const MachineLanguageMapping_t *mapping)
{
    const MachineLanguageHost_t *host = load->space.host;
    const uint64_t length = MACHINE_LANGUAGE_PAGE_BYTES;

    if (mapping->kind == MACHINE_LANGUAGE_MAPPING(CACHED))
        host->unmap_firmware_writable(host->context, mapping->address, length);
    else
        host->unmap_device(host->context, mapping->address, length);
}

/**
 * @brief The entry a new mapping takes: a free one, or the least recently used, given back first.
 */
static MachineLanguageMapping_t *machine_language_free_mapping(MachineLanguageLoad_t *load)
{
    MachineLanguageRegions_t *regions = &load->regions;
    MachineLanguageMapping_t *oldest = &regions->mappings[0];

    if (regions->mapping_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_MAPPINGS)
        return &regions->mappings[regions->mapping_count++];
    for (uint32_t index = 1u; index < regions->mapping_count; ++index)
        if (regions->mappings[index].last_use < oldest->last_use)
            oldest = &regions->mappings[index];
    machine_language_unmap(load, oldest);
    return oldest;
}

/**
 * @brief Maps one page: cached for RAM, ACPI reclaim and NVS memory, uncached elsewhere.
 *
 * @return Where it is mapped, or NULL when the host refuses; @p out_kind receives how.
 */
static void *machine_language_map_page(MachineLanguageLoad_t *load, uint64_t page, uint8_t *out_kind)
{
    const MachineLanguageHost_t *host = load->space.host;
    const uint8_t type = host->memory_type(host->context, page, MACHINE_LANGUAGE_PAGE_BYTES);

    *out_kind = (type == MACHINE_LANGUAGE_MEMORY(RAM) || type == MACHINE_LANGUAGE_MEMORY(ACPI_RECLAIM) ||
                 type == MACHINE_LANGUAGE_MEMORY(ACPI_NVS)) ?
                    MACHINE_LANGUAGE_MAPPING(CACHED) :
                    MACHINE_LANGUAGE_MAPPING(DEVICE);
    return (*out_kind == MACHINE_LANGUAGE_MAPPING(CACHED)) ?
               host->map_firmware_writable(host->context, page, MACHINE_LANGUAGE_PAGE_BYTES) :
               host->map_device(host->context, page, MACHINE_LANGUAGE_PAGE_BYTES);
}

/**
 * @brief Where the cache maps the page that holds @p physical_address: an entry already made, or a
 *        new one, cached for RAM, ACPI reclaim and NVS memory, uncached elsewhere.
 */
static MachineLanguageStatus_t machine_language_map(MachineLanguageLoad_t *load, uint64_t physical_address,
                                                    uint8_t **out_address)
{
    MachineLanguageRegions_t *regions = &load->regions;
    const uint64_t first_page = physical_address & ~(uint64_t) (MACHINE_LANGUAGE_PAGE_BYTES - 1u);

    ++regions->clock;
    for (uint32_t index = 0u; index < regions->mapping_count; ++index)
    {
        MachineLanguageMapping_t *mapping = &regions->mappings[index];

        if (first_page == mapping->physical_address)
        {
            mapping->last_use = regions->clock;
            ++regions->mapping_cache_hits;
            *out_address = (uint8_t *) mapping->address + (physical_address - mapping->physical_address);
            return MACHINE_LANGUAGE_OK;
        }
    }

    uint8_t kind = 0u;
    void *address = machine_language_map_page(load, first_page, &kind);

    ++regions->map_calls;
    if (address == NULL)
        return MACHINE_LANGUAGE_STATUS(ADDRESS_UNREACHABLE);

    MachineLanguageMapping_t *mapping = machine_language_free_mapping(load);

    mapping->physical_address = first_page;
    mapping->address = address;
    mapping->last_use = regions->clock;
    mapping->kind = kind;
    *out_address = (uint8_t *) address + (physical_address - first_page);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief One access of 1, 2 or 4 bytes, aligned on its width, to memory, through the mapping cache.
 */
static MachineLanguageStatus_t machine_language_aligned_memory(MachineLanguageLoad_t *load, uint64_t physical_address,
                                                               uint8_t width, bool write, uint32_t *value)
{
    const MachineLanguageHost_t *host = load->space.host;
    uint8_t *address = NULL;
    const MachineLanguageStatus_t status = machine_language_map(load, physical_address, &address);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (write)
        host->write_memory(host->context, address, physical_address, width, *value);
    else
        *value = host->read_memory(host->context, address, physical_address, width);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief One access of 1, 2 or 4 bytes to memory: one aligned access, or, at an address its width
 *        does not divide, one access per byte, counted, so that no access crosses a page or reads
 *        unaligned.
 */
static MachineLanguageStatus_t machine_language_memory(MachineLanguageLoad_t *load, uint64_t physical_address,
                                                       uint8_t width, bool write, uint32_t *value)
{
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;
    uint32_t assembled = 0u;

    if (physical_address > MACHINE_LANGUAGE_REACHABLE_LAST ||
        width - 1u > MACHINE_LANGUAGE_REACHABLE_LAST - physical_address)
        return MACHINE_LANGUAGE_STATUS(ADDRESS_UNREACHABLE);
    if (physical_address % width == 0u)
        return machine_language_aligned_memory(load, physical_address, width, write, value);
    ++load->regions.unaligned_accesses;
    for (uint8_t index = 0u; index < width && status == MACHINE_LANGUAGE_OK; ++index)
    {
        uint32_t byte = write ? ((*value >> (8u * index)) & 0xFFu) : 0u;

        status = machine_language_aligned_memory(load, physical_address + index, 1u, write, &byte);
        assembled |= byte << (8u * index);
    }
    if (status == MACHINE_LANGUAGE_OK && !write)
        *value = assembled;
    return status;
}

/**
 * @brief One access of 1, 2 or 4 bytes to an I/O port: a protected port reads as 0 and drops a
 *        write, counted.
 */
static MachineLanguageStatus_t machine_language_port(MachineLanguageLoad_t *load, uint64_t port, uint8_t width,
                                                     bool write, uint32_t *value)
{
    const MachineLanguageHost_t *host = load->space.host;

    if (port > 0xFFFFu || width - 1u > 0xFFFFu - port)
        return MACHINE_LANGUAGE_STATUS(ADDRESS_UNREACHABLE);
    if (advanced_configuration_and_power_interface_machine_language_port_is_protected(load, (uint16_t) port))
    {
        ++load->regions.protected_port_accesses;
        if (!write)
            *value = 0u;
        return MACHINE_LANGUAGE_OK;
    }
    if (write)
        host->port_write(host->context, (uint16_t) port, width, *value);
    else
        *value = host->port_read(host->context, (uint16_t) port, width);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Counts one field access against the evaluation's budget (§6.1), and fails once it is spent.
 */
static MachineLanguageStatus_t machine_language_count_field_access(MachineLanguageLoad_t *load)
{
    uint32_t *accesses = &load->evaluation.field_accesses;

    if (*accesses >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FIELD_ACCESSES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_FIELD_ACCESSES);
    if (++*accesses > load->regions.field_accesses_peak)
        load->regions.field_accesses_peak = *accesses;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief One access of up to 4 bytes in a space: memory, a port, a PCI function's configuration
 *        space, or a space with no handler. It is one field access, an unaligned one included, which
 *        fails once the budget is spent.
 */
static MachineLanguageStatus_t machine_language_space_access(MachineLanguageLoad_t *load, uint32_t region_node,
                                                             uint8_t space, uint64_t address, uint8_t width, bool write,
                                                             uint32_t *value)
{
    const MachineLanguageStatus_t status = machine_language_count_field_access(load);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (space == MACHINE_LANGUAGE_SYSTEM_MEMORY)
        return machine_language_memory(load, address, width, write, value);
    if (space == MACHINE_LANGUAGE_SYSTEM_IO)
        return machine_language_port(load, address, width, write, value);
    if (space == MACHINE_LANGUAGE_PCI_CONFIG)
        return advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_access(
            load, region_node, address, width, write, value);
    ++load->regions.space_unsupported[space];
    return MACHINE_LANGUAGE_STATUS(SPACE_UNSUPPORTED);
}

/**
 * @brief One datum of a region, @p byte_offset bytes in: it must fit the region, whose length may be
 *        rounded up to the datum's width, counted; an 8-byte datum of memory is two 4-byte accesses,
 *        low then high (§5.3), and one of a port or of a PCI function is refused, ports and
 *        configuration accesses being at most 4 bytes wide.
 */
static MachineLanguageStatus_t machine_language_region_datum(MachineLanguageLoad_t *load, uint32_t region_node,
                                                             uint64_t byte_offset, uint8_t width, bool write,
                                                             uint64_t *value)
{
    MachineLanguageRegions_t *regions = &load->regions;
    const MachineLanguageRegion_t *region = machine_language_region(load, region_node);

    if (region == NULL)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (region->state == 0u)
        return MACHINE_LANGUAGE_STATUS(REGION_UNEVALUATED);
    if ((region->space == MACHINE_LANGUAGE_SYSTEM_IO || region->space == MACHINE_LANGUAGE_PCI_CONFIG) && width == 8u)
        return MACHINE_LANGUAGE_STATUS(ACCESS_WIDTH_INVALID);
    if (byte_offset + width > region->length)
    {
        const uint64_t rounded =
            (region->length > ~0ull - width) ? ~0ull : (region->length + width - 1u) / width * width;

        if (byte_offset + width > rounded)
            return MACHINE_LANGUAGE_STATUS(REGION_BOUNDS);
        ++regions->region_lengths_rounded;
    }
    if (region->offset > ~0ull - byte_offset - width)
        return MACHINE_LANGUAGE_STATUS(ADDRESS_UNREACHABLE);

    const uint64_t address = region->offset + byte_offset;
    const uint8_t part = (width == 8u) ? 4u : width;
    uint32_t low = (uint32_t) *value;
    uint32_t high = (uint32_t) (*value >> 32u);
    MachineLanguageStatus_t status =
        machine_language_space_access(load, region_node, region->space, address, part, write, &low);

    if (status == MACHINE_LANGUAGE_OK && width == 8u)
    {
        ++regions->split_qword_accesses;
        status = machine_language_space_access(load, region_node, region->space, address + 4u, part, write, &high);
    }
    if (status == MACHINE_LANGUAGE_OK && !write)
        *value = (width == 8u) ? ((uint64_t) high << 32u) | low : low;
    return status;
}

/**
 * @brief Whether the PM1 control register @p index is one GBL_RLS may be written to: given, and not a
 *        port the kernel protects.
 */
static bool machine_language_control_port(const MachineLanguageRegions_t *regions, uint32_t index)
{
    const uint16_t port = regions->power_management_control_ports[index];

    return port != 0u && !advanced_configuration_and_power_interface_port_is_protected(port);
}

/**
 * @brief Tells the firmware waiting for the global lock that it is free: reads PM1a and PM1b control,
 *        ORs them, clears the write-only bits, sets GBL_RLS, and writes the value to PM1a, then to
 *        PM1b, as ACPI reads and writes a register grouping (the ACPI Register Model); counted when a
 *        register was written.
 */
static void machine_language_signal_global_lock(MachineLanguageLoad_t *load)
{
    MachineLanguageRegions_t *regions = &load->regions;
    const MachineLanguageHost_t *host = load->space.host;
    uint32_t value = 0u;
    bool written = false;

    for (uint32_t index = 0u; index < 2u; ++index)
        if (machine_language_control_port(regions, index))
            value |= host->port_read(host->context, regions->power_management_control_ports[index], 2u);
    value = (value & ~MACHINE_LANGUAGE_CONTROL_WRITE_ONLY) | MACHINE_LANGUAGE_LOCK_RELEASE;
    for (uint32_t index = 0u; index < 2u; ++index)
    {
        if (!machine_language_control_port(regions, index))
            continue;
        host->port_write(host->context, regions->power_management_control_ports[index], 2u, value);
        written = true;
    }
    regions->global_lock_releases_signalled += written ? 1u : 0u;
}

/**
 * @brief Takes the global lock for a Lock field (§4.4): marks it owned, or, while the firmware owns
 *        it, pending, then waits for the firmware to release it; an access already holding it nests.
 *        A platform without a global lock takes nothing, counted.
 */
static MachineLanguageStatus_t machine_language_take_global_lock(MachineLanguageLoad_t *load)
{
    MachineLanguageRegions_t *regions = &load->regions;
    const MachineLanguageHost_t *host = load->space.host;
    bool waited = false;

    if (regions->global_lock_word == NULL)
    {
        ++regions->unlocked_lock_accesses;
        return MACHINE_LANGUAGE_OK;
    }
    if (regions->global_lock_depth != 0u)
    {
        ++regions->global_lock_depth;
        return MACHINE_LANGUAGE_OK;
    }

    for (uint32_t poll = 0u; poll < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_GLOBAL_LOCK_POLLS;
         ++poll)
    {
        const MachineLanguageStatus_t status = machine_language_count_field_access(load);

        if (status != MACHINE_LANGUAGE_OK)
            return status;

        const uint32_t held =
            host->read_memory(host->context, regions->global_lock_word, regions->global_lock_address, 4u);
        const bool owned = (held & MACHINE_LANGUAGE_LOCK_OWNED) != 0u;
        const uint32_t wanted = (held & ~(MACHINE_LANGUAGE_LOCK_OWNED | MACHINE_LANGUAGE_LOCK_PENDING)) |
                                MACHINE_LANGUAGE_LOCK_OWNED | (owned ? MACHINE_LANGUAGE_LOCK_PENDING : 0u);

        if ((owned && waited) || host->compare_exchange(host->context, regions->global_lock_word,
                                                        regions->global_lock_address, held, wanted) != held)
            continue;
        if (owned)
        {
            waited = true;
            continue;
        }
        ++regions->global_lock_acquisitions;
        regions->global_lock_waits += waited ? 1u : 0u;
        regions->global_lock_depth = 1u;
        return MACHINE_LANGUAGE_OK;
    }
    ++regions->global_lock_timeouts;
    return MACHINE_LANGUAGE_STATUS(GLOBAL_LOCK_UNAVAILABLE);
}

/**
 * @brief Gives back the global lock the last access holding it took: clears owned and pending, and
 *        signals the firmware when it had set pending.
 */
static void machine_language_release_global_lock(MachineLanguageLoad_t *load)
{
    MachineLanguageRegions_t *regions = &load->regions;
    const MachineLanguageHost_t *host = load->space.host;

    if (regions->global_lock_word == NULL || --regions->global_lock_depth != 0u)
        return;
    for (uint32_t poll = 0u; poll < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_GLOBAL_LOCK_POLLS;
         ++poll)
    {
        const uint32_t held =
            host->read_memory(host->context, regions->global_lock_word, regions->global_lock_address, 4u);

        if (host->compare_exchange(host->context, regions->global_lock_word, regions->global_lock_address, held,
                                   held & ~(MACHINE_LANGUAGE_LOCK_OWNED | MACHINE_LANGUAGE_LOCK_PENDING)) != held)
            continue;
        if ((held & MACHINE_LANGUAGE_LOCK_PENDING) != 0u)
            machine_language_signal_global_lock(load);
        return;
    }
}

/**
 * @brief Reads or writes the datums of a register of an IndexField or a BankField, in its region.
 */
static MachineLanguageStatus_t machine_language_access_register(MachineLanguageLoad_t *load,
                                                                const MachineLanguageFieldUnit_t *unit, bool write,
                                                                uint64_t *value)
{
    const uint8_t width = machine_language_access_bytes(unit->access);
    const uint32_t datum_bits = 8u * width;
    const uint64_t end = (uint64_t) unit->bit_offset + unit->bit_length;
    const uint64_t written = *value;
    uint64_t result = 0u;

    for (uint64_t first = (unit->bit_offset / datum_bits) * datum_bits; first < end; first += datum_bits)
    {
        const MachineLanguageDatum_t datum = machine_language_datum(first, datum_bits, unit->bit_offset, end);
        const uint64_t at = datum.first_bit + datum.shift - unit->bit_offset;
        uint64_t raw = 0u;
        MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

        if (write && datum.count < datum_bits && unit->update != MACHINE_LANGUAGE_UPDATE_ONES &&
            unit->update != MACHINE_LANGUAGE_UPDATE_ZEROS)
            status = machine_language_region_datum(load, unit->region, first / 8u, width, false, &raw);
        if (status == MACHINE_LANGUAGE_OK && write)
        {
            raw = machine_language_merge(unit->update, raw, written >> at, &datum, datum_bits);
            status = machine_language_region_datum(load, unit->region, first / 8u, width, true, &raw);
        }
        else if (status == MACHINE_LANGUAGE_OK)
        {
            status = machine_language_region_datum(load, unit->region, first / 8u, width, false, &raw);
            result |= ((raw >> datum.shift) & machine_language_low_bits(datum.count)) << at;
        }
        if (status != MACHINE_LANGUAGE_OK)
            return status;
    }
    if (!write)
        *value = result;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Reads or writes a register of an IndexField or a BankField: a unit of a Field, no wider
 *        than 64 bits, accessed datum by datum in its region.
 */
static MachineLanguageStatus_t machine_language_register(MachineLanguageLoad_t *load, uint32_t node, bool write,
                                                         uint64_t *value)
{
    const MachineLanguageFieldUnit_t *unit = machine_language_unit(load, node);

    if (unit == NULL || unit->kind != MACHINE_LANGUAGE_FIELD_UNIT(NORMAL) || unit->bit_length > 64u)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (unit->access > MACHINE_LANGUAGE_ACCESS_LAST)
        return MACHINE_LANGUAGE_STATUS(ACCESS_WIDTH_INVALID);

    MachineLanguageStatus_t status = (unit->lock != 0u) ? machine_language_take_global_lock(load) : MACHINE_LANGUAGE_OK;

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    status = machine_language_access_register(load, unit, write, value);
    if (unit->lock != 0u)
        machine_language_release_global_lock(load);
    return status;
}

/**
 * @brief Whether @p selector, an index or a BankValue, fits the register at @p node; a register that is
 *        not a unit of a Field fits anything here, and is refused when it is written.
 */
static bool machine_language_selector_fits(const MachineLanguageLoad_t *load, uint32_t node, uint64_t selector)
{
    const MachineLanguageFieldUnit_t *unit = machine_language_unit(load, node);

    return unit == NULL || unit->bit_length >= 64u || (selector >> unit->bit_length) == 0u;
}

/**
 * @brief One datum of a unit, at @p byte_offset from the start of its field list: in its region for
 *        a Field; once its BankValue is in its bank register for a BankField; through its data
 *        register, once its index register holds @p byte_offset, for an IndexField. An index or a
 *        BankValue its register cannot hold is refused before anything is written.
 */
static MachineLanguageStatus_t machine_language_unit_datum(MachineLanguageLoad_t *load,
                                                           const MachineLanguageFieldUnit_t *unit, uint64_t byte_offset,
                                                           uint8_t width, bool write, uint64_t *value)
{
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;
    uint64_t selector = (unit->kind == MACHINE_LANGUAGE_FIELD_UNIT(BANK)) ? unit->bank_value : byte_offset;

    if (unit->kind == MACHINE_LANGUAGE_FIELD_UNIT(NORMAL))
        return machine_language_region_datum(load, unit->region, byte_offset, width, write, value);
    if (!machine_language_selector_fits(
            load, (unit->kind == MACHINE_LANGUAGE_FIELD_UNIT(BANK)) ? unit->data_or_bank : unit->region, selector))
        return MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);
    if (unit->kind == MACHINE_LANGUAGE_FIELD_UNIT(BANK))
    {
        status = machine_language_register(load, unit->data_or_bank, true, &selector);
        return (status == MACHINE_LANGUAGE_OK) ?
                   machine_language_region_datum(load, unit->region, byte_offset, width, write, value) :
                   status;
    }
    status = machine_language_register(load, unit->region, true, &selector);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_register(load, unit->data_or_bank, write, value);
    if (status == MACHINE_LANGUAGE_OK && !write)
        *value &= machine_language_low_bits(8u * width);
    return status;
}

/**
 * @brief Reads a unit that one datum holds whole into @p out_integer.
 */
static MachineLanguageStatus_t machine_language_read_one_datum(MachineLanguageLoad_t *load,
                                                               const MachineLanguageFieldUnit_t *unit, uint8_t width,
                                                               uint64_t *out_integer)
{
    const uint32_t datum_bits = 8u * width;
    const uint64_t first = (unit->bit_offset / datum_bits) * datum_bits;
    uint64_t raw = 0u;
    const MachineLanguageStatus_t status = machine_language_unit_datum(load, unit, first / 8u, width, false, &raw);

    *out_integer = (raw >> (unit->bit_offset - first)) & machine_language_low_bits(unit->bit_length);
    return status;
}

/**
 * @brief Reads every datum of a unit into @p bytes, which hold its bits from bit 0.
 */
static MachineLanguageStatus_t machine_language_read_datums(MachineLanguageLoad_t *load,
                                                            const MachineLanguageFieldUnit_t *unit, uint8_t width,
                                                            uint8_t *bytes)
{
    const uint32_t datum_bits = 8u * width;
    const uint64_t end = (uint64_t) unit->bit_offset + unit->bit_length;

    for (uint64_t first = (unit->bit_offset / datum_bits) * datum_bits; first < end; first += datum_bits)
    {
        const MachineLanguageDatum_t datum = machine_language_datum(first, datum_bits, unit->bit_offset, end);
        uint64_t raw = 0u;
        const MachineLanguageStatus_t status = machine_language_unit_datum(load, unit, first / 8u, width, false, &raw);

        if (status != MACHINE_LANGUAGE_OK)
            return status;
        machine_language_put_bits(bytes, datum.first_bit + datum.shift - unit->bit_offset, raw >> datum.shift,
                                  datum.count);
    }
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The bytes a value stores into a field: an Integer's, little-endian, or a String's or a
 *        Buffer's; anything else is refused.
 */
static MachineLanguageStatus_t machine_language_source_bytes(const MachineLanguageLoad_t *load,
                                                             const MachineLanguageValue_t *value, uint8_t *integer,
                                                             const uint8_t **out_bytes, uint32_t *out_length)
{
    if (value->kind == MACHINE_LANGUAGE_VALUE(INTEGER))
    {
        for (uint32_t index = 0u; index < 8u; ++index)
            integer[index] = (uint8_t) (value->payload >> (8u * index));
        *out_bytes = integer;
        *out_length = (load->integer_bits == 32u) ? 4u : 8u;
        return MACHINE_LANGUAGE_OK;
    }
    if (value->kind != MACHINE_LANGUAGE_VALUE(STRING) && value->kind != MACHINE_LANGUAGE_VALUE(BUFFER))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *header =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, value->handle);

    if (header == NULL)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    *out_bytes = (const uint8_t *) (header + 1);
    *out_length = header->length;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Reads a unit, the global lock already taken when it needs it.
 */
static MachineLanguageStatus_t machine_language_read_unit(MachineLanguageLoad_t *load,
                                                          const MachineLanguageFieldUnit_t *unit,
                                                          MachineLanguageValue_t *out_value)
{
    const uint32_t integer_bits = (load->integer_bits == 32u) ? 32u : 64u;
    uint8_t integer[8] = {0};
    uint32_t handle = 0u;
    uint8_t *bytes = integer;

    ++load->regions.field_reads;

    const uint8_t width = machine_language_access_bytes(unit->access);
    const uint32_t datum_bits = 8u * width;
    const uint32_t length = (uint32_t) (((uint64_t) unit->bit_length + 7u) / 8u);

    if (unit->bit_length == 0u)
    {
        *out_value = (MachineLanguageValue_t){.kind = MACHINE_LANGUAGE_VALUE(INTEGER), .payload = 0u};
        return MACHINE_LANGUAGE_OK;
    }
    if (unit->bit_length <= integer_bits && unit->bit_offset % datum_bits + unit->bit_length <= datum_bits)
    {
        uint64_t value = 0u;
        const MachineLanguageStatus_t status = machine_language_read_one_datum(load, unit, width, &value);

        *out_value = (MachineLanguageValue_t){.kind = MACHINE_LANGUAGE_VALUE(INTEGER), .payload = value};
        return status;
    }
    if (unit->bit_length > integer_bits)
    {
        if (length > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES)
            return MACHINE_LANGUAGE_STATUS(LIMIT_BUFFER_BYTES);

        const MachineLanguageStatus_t status =
            advanced_configuration_and_power_interface_machine_language_object_allocate(
                &load->heap, &load->space, MACHINE_LANGUAGE_VALUE(BUFFER), length, &handle);

        if (status != MACHINE_LANGUAGE_OK)
            return status;
        bytes =
            (uint8_t *) (advanced_configuration_and_power_interface_machine_language_object(&load->heap, handle) + 1);
    }

    const MachineLanguageStatus_t status = machine_language_read_datums(load, unit, width, bytes);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    *out_value = (unit->bit_length > integer_bits) ?
                     (MachineLanguageValue_t){.kind = MACHINE_LANGUAGE_VALUE(BUFFER), .handle = handle} :
                     (MachineLanguageValue_t){.kind = MACHINE_LANGUAGE_VALUE(INTEGER),
                                              .payload = machine_language_get_bits(integer, 8u, 0u, 64u)};
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Gives a node a heap object of @p kind holding the @p bytes of @p shape.
 */
static MachineLanguageStatus_t machine_language_build_object(MachineLanguageLoad_t *load, uint32_t node, uint8_t kind,
                                                             const void *shape, uint32_t bytes)
{
    uint32_t handle = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_object_allocate(
        &load->heap, &load->space, kind, bytes, &handle);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    memcpy(advanced_configuration_and_power_interface_machine_language_object(&load->heap, handle) + 1, shape, bytes);

    MachineLanguageNode_t *target =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node);

    target->value.kind = kind;
    target->value.handle = handle;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Writes a unit, the global lock already taken when it needs it.
 */
static MachineLanguageStatus_t machine_language_write_unit(MachineLanguageLoad_t *load,
                                                           const MachineLanguageFieldUnit_t *unit,
                                                           const MachineLanguageValue_t *value)
{
    uint8_t integer[8] = {0};
    const uint8_t *source = NULL;
    uint32_t source_length = 0u;

    if (((uint64_t) unit->bit_length + 7u) / 8u >
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_BUFFER_BYTES);

    MachineLanguageStatus_t status = machine_language_source_bytes(load, value, integer, &source, &source_length);

    if (status != MACHINE_LANGUAGE_OK || unit->bit_length == 0u)
        return status;
    ++load->regions.field_writes;

    const uint8_t width = machine_language_access_bytes(unit->access);
    const uint32_t datum_bits = 8u * width;
    const uint64_t end = (uint64_t) unit->bit_offset + unit->bit_length;

    for (uint64_t first = (unit->bit_offset / datum_bits) * datum_bits; first < end; first += datum_bits)
    {
        const MachineLanguageDatum_t datum = machine_language_datum(first, datum_bits, unit->bit_offset, end);
        const uint64_t bits = machine_language_get_bits(source, source_length,
                                                        datum.first_bit + datum.shift - unit->bit_offset, datum.count);
        uint64_t raw = 0u;

        if (datum.count < datum_bits && unit->update != MACHINE_LANGUAGE_UPDATE_ONES &&
            unit->update != MACHINE_LANGUAGE_UPDATE_ZEROS)
            status = machine_language_unit_datum(load, unit, first / 8u, width, false, &raw);
        raw = machine_language_merge(unit->update, raw, bits, &datum, datum_bits);
        if (status == MACHINE_LANGUAGE_OK)
            status = machine_language_unit_datum(load, unit, first / 8u, width, true, &raw);
        if (status != MACHINE_LANGUAGE_OK)
            return status;
    }
    return MACHINE_LANGUAGE_OK;
}

MachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_field_read(MachineLanguageLoad_t *load, uint32_t node,
                                                                       MachineLanguageValue_t *out_value)
{
    const MachineLanguageFieldUnit_t *unit = machine_language_unit(load, node);

    if (unit == NULL)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (unit->access > MACHINE_LANGUAGE_ACCESS_LAST)
        return MACHINE_LANGUAGE_STATUS(ACCESS_WIDTH_INVALID);

    MachineLanguageStatus_t status = (unit->lock != 0u) ? machine_language_take_global_lock(load) : MACHINE_LANGUAGE_OK;

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    status = machine_language_read_unit(load, unit, out_value);
    if (unit->lock != 0u)
        machine_language_release_global_lock(load);
    return status;
}

MachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_field_write(MachineLanguageLoad_t *load, uint32_t node,
                                                                        const MachineLanguageValue_t *value)
{
    const MachineLanguageFieldUnit_t *unit = machine_language_unit(load, node);

    if (unit == NULL)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (unit->access > MACHINE_LANGUAGE_ACCESS_LAST)
        return MACHINE_LANGUAGE_STATUS(ACCESS_WIDTH_INVALID);

    MachineLanguageStatus_t status = (unit->lock != 0u) ? machine_language_take_global_lock(load) : MACHINE_LANGUAGE_OK;

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    status = machine_language_write_unit(load, unit, value);
    if (unit->lock != 0u)
        machine_language_release_global_lock(load);
    return status;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_region_build(
    MachineLanguageLoad_t *load, uint32_t node, uint8_t space, uint64_t offset, uint64_t length)
{
    const MachineLanguageRegion_t region = {
        .space = space,
        .state = 1u,
        .offset = offset,
        .length = length,
    };

    return machine_language_build_object(load, node, MACHINE_LANGUAGE_VALUE(REGION), &region,
                                         (uint32_t) sizeof(region));
}

MachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_field_unit_build(MachineLanguageLoad_t *load, uint32_t node,
                                                                             const MachineLanguageFieldUnit_t *shape)
{
    return machine_language_build_object(load, node, MACHINE_LANGUAGE_VALUE(FIELD_UNIT), shape,
                                         (uint32_t) sizeof(*shape));
}

void advanced_configuration_and_power_interface_machine_language_use_global_lock(MachineLanguageLoad_t *load,
                                                                                 uint32_t firmware_control,
                                                                                 const uint16_t *control_ports)
{
    MachineLanguageRegions_t *regions = &load->regions;
    const MachineLanguageHost_t *host = load->space.host;
    const uint8_t *facs = NULL;
    uint32_t length = 0u;
    bool usable = false;

    regions->power_management_control_ports[0] = control_ports[0];
    regions->power_management_control_ports[1] = control_ports[1];
    regions->facs_refused = firmware_control != 0u;
    if (firmware_control == 0u || firmware_control % MACHINE_LANGUAGE_FACS_ALIGNMENT != 0u ||
        host->memory_type(host->context, firmware_control, MACHINE_LANGUAGE_FACS_MINIMUM) ==
            MACHINE_LANGUAGE_MEMORY(RAM))
        return;
    facs = host->map_firmware(host->context, firmware_control, MACHINE_LANGUAGE_FACS_MINIMUM);
    if (facs == NULL)
        return;
    memcpy(&length, facs + 4u, sizeof(length));
    usable = memcmp(facs, "FACS", 4u) == 0 && length >= MACHINE_LANGUAGE_FACS_MINIMUM;
    host->unmap_firmware(host->context, facs, MACHINE_LANGUAGE_FACS_MINIMUM);

    const uint64_t address = (uint64_t) firmware_control + MACHINE_LANGUAGE_FACS_LOCK_OFFSET;
    const uint64_t page = address & ~(uint64_t) (MACHINE_LANGUAGE_PAGE_BYTES - 1u);
    uint8_t *mapped = usable ? machine_language_map_page(load, page, &regions->global_lock_mapping_kind) : NULL;

    if (mapped == NULL)
        return;
    regions->global_lock_address = address;
    regions->global_lock_word = mapped + (address - page);
    regions->facs_refused = false;
}

void advanced_configuration_and_power_interface_machine_language_regions_close(MachineLanguageLoad_t *load)
{
    MachineLanguageRegions_t *regions = &load->regions;
    const MachineLanguageHost_t *host = load->space.host;

    if (regions->global_lock_word != NULL)
    {
        void *page =
            regions->global_lock_word - (regions->global_lock_address & (uint64_t) (MACHINE_LANGUAGE_PAGE_BYTES - 1u));

        if (regions->global_lock_mapping_kind == MACHINE_LANGUAGE_MAPPING(CACHED))
            host->unmap_firmware_writable(host->context, page, MACHINE_LANGUAGE_PAGE_BYTES);
        else
            host->unmap_device(host->context, page, MACHINE_LANGUAGE_PAGE_BYTES);
        regions->global_lock_word = NULL;
    }

    while (regions->mapping_count > 0u)
        machine_language_unmap(load, &regions->mappings[--regions->mapping_count]);
}

MachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_take_global_lock(MachineLanguageLoad_t *load)
{
    return machine_language_take_global_lock(load);
}

void advanced_configuration_and_power_interface_machine_language_release_global_lock(MachineLanguageLoad_t *load)
{
    machine_language_release_global_lock(load);
}
