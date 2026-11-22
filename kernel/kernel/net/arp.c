#include <kernel/net/arp.h>
#include <kernel/net/byte_order.h>

#include <string.h>

#define KERNEL_ADDRESS_RESOLUTION_HARDWARE_ETHERNET                    1u      /**< HTYPE of Ethernet. */
#define KERNEL_ADDRESS_RESOLUTION_PROTOCOL_INTERNET_PROTOCOL_VERSION_4 0x0800u /**< PTYPE of IPv4. */
#define KERNEL_ADDRESS_RESOLUTION_INTERNET_PROTOCOL_VERSION_4_BYTES    4u      /**< PLEN of IPv4. */

/**
 * @brief Whether @p entry is free, or holds what its lifetime let go of.
 */
static bool kernel_address_resolution_cache_is_spent(const KernelAddressResolutionCache_t *cache,
                                                     const KernelAddressResolutionEntry_t *entry, uint32_t now)
{
    return entry->address == 0u || now - entry->since >= cache->lifetime;
}

/**
 * @brief The entry for @p address, or NULL.
 */
static KernelAddressResolutionEntry_t *kernel_address_resolution_cache_entry(KernelAddressResolutionCache_t *cache,
                                                                             uint32_t address)
{
    if (address == 0u)
        return NULL;
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
        if (cache->entries[entry].address == address)
            return &cache->entries[entry];
    return NULL;
}

/**
 * @brief The entry a new address takes: a spent one, failing that the oldest not wanted, failing that, when
 *        @p may_evict_wanted, the oldest.
 *
 * @return The entry, or NULL when every one is wanted and alive and @p may_evict_wanted is false.
 */
static KernelAddressResolutionEntry_t *kernel_address_resolution_cache_room(KernelAddressResolutionCache_t *cache,
                                                                            uint32_t now, bool may_evict_wanted)
{
    KernelAddressResolutionEntry_t *oldest_unwanted = NULL;
    KernelAddressResolutionEntry_t *oldest = &cache->entries[0];

    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
    {
        KernelAddressResolutionEntry_t *const candidate = &cache->entries[entry];

        if (kernel_address_resolution_cache_is_spent(cache, candidate, now))
            return candidate;
        if (now - candidate->since > now - oldest->since)
            oldest = candidate;
        if (!candidate->wanted && (oldest_unwanted == NULL || now - candidate->since > now - oldest_unwanted->since))
            oldest_unwanted = candidate;
    }
    if (oldest_unwanted != NULL)
        return oldest_unwanted;
    return may_evict_wanted ? oldest : NULL;
}

bool kernel_address_resolution_parse(const uint8_t *bytes, uint32_t length, KernelAddressResolutionPacket_t *out)
{
    if (length < KERNEL_ADDRESS_RESOLUTION_BYTES ||
        kernel_network_read_16(bytes) != KERNEL_ADDRESS_RESOLUTION_HARDWARE_ETHERNET ||
        kernel_network_read_16(bytes + 2u) != KERNEL_ADDRESS_RESOLUTION_PROTOCOL_INTERNET_PROTOCOL_VERSION_4 ||
        bytes[4] != KERNEL_NETWORK_ADDRESS_BYTES ||
        bytes[5] != KERNEL_ADDRESS_RESOLUTION_INTERNET_PROTOCOL_VERSION_4_BYTES)
        return false;
    out->operation = kernel_network_read_16(bytes + 6u);
    memcpy(out->sender_station, bytes + 8u, KERNEL_NETWORK_ADDRESS_BYTES);
    out->sender_address = kernel_network_read_32(bytes + 14u);
    memcpy(out->target_station, bytes + 18u, KERNEL_NETWORK_ADDRESS_BYTES);
    out->target_address = kernel_network_read_32(bytes + 24u);
    return true;
}

void kernel_address_resolution_write(uint8_t *bytes, const KernelAddressResolutionPacket_t *packet)
{
    kernel_network_write_16(bytes, KERNEL_ADDRESS_RESOLUTION_HARDWARE_ETHERNET);
    kernel_network_write_16(bytes + 2u, KERNEL_ADDRESS_RESOLUTION_PROTOCOL_INTERNET_PROTOCOL_VERSION_4);
    bytes[4] = KERNEL_NETWORK_ADDRESS_BYTES;
    bytes[5] = KERNEL_ADDRESS_RESOLUTION_INTERNET_PROTOCOL_VERSION_4_BYTES;
    kernel_network_write_16(bytes + 6u, packet->operation);
    memcpy(bytes + 8u, packet->sender_station, KERNEL_NETWORK_ADDRESS_BYTES);
    kernel_network_write_32(bytes + 14u, packet->sender_address);
    memcpy(bytes + 18u, packet->target_station, KERNEL_NETWORK_ADDRESS_BYTES);
    kernel_network_write_32(bytes + 24u, packet->target_address);
}

void kernel_address_resolution_cache_initialize(KernelAddressResolutionCache_t *cache, uint32_t lifetime)
{
    memset(cache, 0, sizeof(*cache));
    cache->lifetime = lifetime;
}

void kernel_address_resolution_cache_ask(KernelAddressResolutionCache_t *cache, uint32_t address, uint32_t now)
{
    KernelAddressResolutionEntry_t *entry = kernel_address_resolution_cache_entry(cache, address);

    if (entry != NULL && entry->resolved && !kernel_address_resolution_cache_is_spent(cache, entry, now))
    {
        entry->wanted = true;
        return;
    }
    if (entry == NULL)
        entry = kernel_address_resolution_cache_room(cache, now, true);
    *entry = (KernelAddressResolutionEntry_t){.address = address, .resolved = false, .wanted = true, .since = now};
}

bool kernel_address_resolution_cache_merge(KernelAddressResolutionCache_t *cache, uint32_t address,
                                           const uint8_t *station, uint32_t now)
{
    KernelAddressResolutionEntry_t *const entry = kernel_address_resolution_cache_entry(cache, address);

    if (entry == NULL)
        return false;
    memcpy(entry->station, station, KERNEL_NETWORK_ADDRESS_BYTES);
    entry->resolved = true;
    entry->since = now;
    return true;
}

bool kernel_address_resolution_cache_add(KernelAddressResolutionCache_t *cache, uint32_t address,
                                         const uint8_t *station, uint32_t now)
{
    KernelAddressResolutionEntry_t *const entry = kernel_address_resolution_cache_room(cache, now, false);

    if (entry == NULL)
        return false;
    *entry = (KernelAddressResolutionEntry_t){.address = address, .resolved = true, .wanted = false, .since = now};
    memcpy(entry->station, station, KERNEL_NETWORK_ADDRESS_BYTES);
    return true;
}

bool kernel_address_resolution_cache_find(const KernelAddressResolutionCache_t *cache, uint32_t address, uint32_t now,
                                          uint8_t *out_station)
{
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
    {
        const KernelAddressResolutionEntry_t *const candidate = &cache->entries[entry];

        if (address == 0u || candidate->address != address)
            continue;
        if (!candidate->resolved || now - candidate->since >= cache->lifetime)
            return false;
        memcpy(out_station, candidate->station, KERNEL_NETWORK_ADDRESS_BYTES);
        return true;
    }
    return false;
}

bool kernel_address_resolution_cache_is_pending(const KernelAddressResolutionCache_t *cache, uint32_t address,
                                                uint32_t now, uint32_t interval)
{
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
    {
        const KernelAddressResolutionEntry_t *const candidate = &cache->entries[entry];

        if (address != 0u && candidate->address == address)
            return !candidate->resolved && now - candidate->since < interval;
    }
    return false;
}

void kernel_address_resolution_cache_expire(KernelAddressResolutionCache_t *cache, uint32_t now)
{
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
        if (cache->entries[entry].address != 0u && now - cache->entries[entry].since >= cache->lifetime)
            cache->entries[entry].address = 0u;
}
