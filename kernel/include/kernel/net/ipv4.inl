#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>

#include <stdbool.h>
#include <stdint.h>

static inline void kernel_internet_protocol_write_header(uint8_t *bytes, uint32_t source, uint32_t destination,
                                                         uint8_t protocol, uint32_t payload_bytes,
                                                         uint16_t identification, uint8_t time_to_live)
{
    const uint32_t total_bytes = KERNEL_INTERNET_PROTOCOL_HEADER_BYTES + payload_bytes;
    const uint32_t first_word = KERNEL_INTERNET_PROTOCOL_VERSION_4 << 12 | KERNEL_INTERNET_PROTOCOL_HEADER_BYTES / 4u
                                                                               << 8;
    const uint32_t lifetime_word = (uint32_t) time_to_live << 8 | protocol;
    const uint32_t sum = first_word + total_bytes + identification + KERNEL_INTERNET_PROTOCOL_DONT_FRAGMENT +
                         lifetime_word + (source >> 16) + (source & 0xFFFFu) + (destination >> 16) +
                         (destination & 0xFFFFu);

    kernel_network_write_16(bytes, (uint16_t) first_word);
    kernel_network_write_16(bytes + 2u, (uint16_t) total_bytes);
    kernel_network_write_16(bytes + 4u, identification);
    kernel_network_write_16(bytes + 6u, KERNEL_INTERNET_PROTOCOL_DONT_FRAGMENT);
    kernel_network_write_16(bytes + 8u, (uint16_t) lifetime_word);
    kernel_network_write_16(bytes + 10u, kernel_internet_checksum_finish(sum));
    kernel_network_write_32(bytes + 12u, source);
    kernel_network_write_32(bytes + 16u, destination);
}

static inline KernelInternetProtocolSubnet_t kernel_internet_protocol_subnet(uint32_t address, uint32_t netmask)
{
    return (KernelInternetProtocolSubnet_t){.network = address & netmask,
                                            .broadcast = address | ~netmask,
                                            .has_broadcast = (~netmask & 0xFFFFFFFEu) != 0u};
}

static inline bool kernel_internet_protocol_source_is_acceptable_in(uint32_t source, uint32_t address,
                                                                    const KernelInternetProtocolSubnet_t *subnet)
{
    const uint32_t first_byte = source >> 24;

    if (first_byte - 1u >= KERNEL_INTERNET_PROTOCOL_FIRST_MULTICAST_NETWORK - 1u ||
        first_byte == KERNEL_INTERNET_PROTOCOL_LOOPBACK_NETWORK || source == address)
        return false;
    return !(subnet->has_broadcast && (source == subnet->network || source == subnet->broadcast));
}
