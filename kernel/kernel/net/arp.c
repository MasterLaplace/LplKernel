#include <kernel/net/arp.h>
#include <kernel/net/byte_order.h>

#include <string.h>

#define KERNEL_ADDRESS_RESOLUTION_HARDWARE_ETHERNET 1u      /**< HTYPE of Ethernet. */
#define KERNEL_ADDRESS_RESOLUTION_PROTOCOL_IPV4     0x0800u /**< PTYPE of IPv4. */
#define KERNEL_ADDRESS_RESOLUTION_IPV4_BYTES        4u      /**< PLEN of IPv4. */

bool kernel_address_resolution_parse(const uint8_t *bytes, uint32_t length, KernelAddressResolutionPacket_t *out)
{
    if (length < KERNEL_ADDRESS_RESOLUTION_BYTES ||
        kernel_network_read_16(bytes) != KERNEL_ADDRESS_RESOLUTION_HARDWARE_ETHERNET ||
        kernel_network_read_16(bytes + 2u) != KERNEL_ADDRESS_RESOLUTION_PROTOCOL_IPV4 ||
        bytes[4] != KERNEL_NETWORK_ADDRESS_BYTES || bytes[5] != KERNEL_ADDRESS_RESOLUTION_IPV4_BYTES)
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
    kernel_network_write_16(bytes + 2u, KERNEL_ADDRESS_RESOLUTION_PROTOCOL_IPV4);
    bytes[4] = KERNEL_NETWORK_ADDRESS_BYTES;
    bytes[5] = KERNEL_ADDRESS_RESOLUTION_IPV4_BYTES;
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

void kernel_address_resolution_cache_learn(KernelAddressResolutionCache_t *cache, uint32_t address,
                                           const uint8_t *station, uint32_t now)
{
    KernelAddressResolutionEntry_t *chosen = &cache->entries[0];

    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
    {
        KernelAddressResolutionEntry_t *const candidate = &cache->entries[entry];

        if (candidate->address == address)
        {
            chosen = candidate;
            break;
        }
        if (chosen->address != 0u &&
            (candidate->address == 0u || now - candidate->learned_at > now - chosen->learned_at))
            chosen = candidate;
    }
    chosen->address = address;
    memcpy(chosen->station, station, KERNEL_NETWORK_ADDRESS_BYTES);
    chosen->learned_at = now;
}

bool kernel_address_resolution_cache_find(const KernelAddressResolutionCache_t *cache, uint32_t address, uint32_t now,
                                          uint8_t *out_station)
{
    for (uint32_t entry = 0u; entry < KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES; ++entry)
    {
        const KernelAddressResolutionEntry_t *const candidate = &cache->entries[entry];

        if (address == 0u || candidate->address != address)
            continue;
        if (now - candidate->learned_at >= cache->lifetime)
            return false;
        memcpy(out_station, candidate->station, KERNEL_NETWORK_ADDRESS_BYTES);
        return true;
    }
    return false;
}
