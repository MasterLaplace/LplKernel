#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/ipv4.h>

#define KERNEL_INTERNET_PROTOCOL_VERSION_4      4u      /**< Version, in the high nibble of the first byte. */
#define KERNEL_INTERNET_PROTOCOL_MORE_FRAGMENTS 0x2000u /**< MF, in the flags and offset field. */
#define KERNEL_INTERNET_PROTOCOL_OFFSET_MASK    0x1FFFu /**< The fragment offset. */
#define KERNEL_INTERNET_PROTOCOL_DONT_FRAGMENT  0x4000u /**< DF, set on every packet sent. */

KernelInternetProtocolStatus_t kernel_internet_protocol_parse(const uint8_t *bytes, uint32_t length,
                                                              KernelInternetProtocolPacket_t *out)
{
    if (length < KERNEL_INTERNET_PROTOCOL_HEADER_BYTES)
        return KERNEL_INTERNET_PROTOCOL_TRUNCATED;
    if (bytes[0] >> 4 != KERNEL_INTERNET_PROTOCOL_VERSION_4)
        return KERNEL_INTERNET_PROTOCOL_NOT_VERSION_4;

    const uint32_t header_bytes = (bytes[0] & 0x0Fu) * 4u;
    const uint32_t total_bytes = kernel_network_read_16(bytes + 2u);
    const uint32_t fragment = kernel_network_read_16(bytes + 6u);

    if (header_bytes < KERNEL_INTERNET_PROTOCOL_HEADER_BYTES)
        return KERNEL_INTERNET_PROTOCOL_BAD_HEADER_LENGTH;
    if (total_bytes < header_bytes)
        return KERNEL_INTERNET_PROTOCOL_BAD_TOTAL_LENGTH;
    if (total_bytes > length)
        return KERNEL_INTERNET_PROTOCOL_TRUNCATED;
    if (kernel_internet_checksum_finish(kernel_internet_checksum_add(0u, bytes, header_bytes)) != 0u)
        return KERNEL_INTERNET_PROTOCOL_BAD_CHECKSUM;
    if ((fragment & (KERNEL_INTERNET_PROTOCOL_MORE_FRAGMENTS | KERNEL_INTERNET_PROTOCOL_OFFSET_MASK)) != 0u)
        return KERNEL_INTERNET_PROTOCOL_FRAGMENT;
    out->source = kernel_network_read_32(bytes + 12u);
    out->destination = kernel_network_read_32(bytes + 16u);
    out->protocol = bytes[9];
    out->payload = bytes + header_bytes;
    out->payload_bytes = total_bytes - header_bytes;
    return KERNEL_INTERNET_PROTOCOL_TAKEN;
}

bool kernel_internet_protocol_source_is_acceptable(uint32_t source, uint32_t address, uint32_t netmask)
{
    const uint32_t first_byte = source >> 24;
    const bool has_broadcast = (~netmask & 0xFFFFFFFEu) != 0u;
    const bool on_subnet = ((source ^ address) & netmask) == 0u;
    const uint32_t host = source & ~netmask;

    if (first_byte == 0u || first_byte == KERNEL_INTERNET_PROTOCOL_LOOPBACK_NETWORK ||
        first_byte >= KERNEL_INTERNET_PROTOCOL_FIRST_MULTICAST_NETWORK || source == address)
        return false;
    return !(has_broadcast && on_subnet && (host == 0u || host == ~netmask));
}

void kernel_internet_protocol_write_header(uint8_t *bytes, uint32_t source, uint32_t destination, uint8_t protocol,
                                           uint32_t payload_bytes, uint16_t identification)
{
    bytes[0] = (uint8_t) (KERNEL_INTERNET_PROTOCOL_VERSION_4 << 4 | KERNEL_INTERNET_PROTOCOL_HEADER_BYTES / 4u);
    bytes[1] = 0u;
    kernel_network_write_16(bytes + 2u, (uint16_t) (KERNEL_INTERNET_PROTOCOL_HEADER_BYTES + payload_bytes));
    kernel_network_write_16(bytes + 4u, identification);
    kernel_network_write_16(bytes + 6u, KERNEL_INTERNET_PROTOCOL_DONT_FRAGMENT);
    bytes[8] = KERNEL_INTERNET_PROTOCOL_TIME_TO_LIVE;
    bytes[9] = protocol;
    kernel_network_write_16(bytes + 10u, 0u);
    kernel_network_write_32(bytes + 12u, source);
    kernel_network_write_32(bytes + 16u, destination);
    kernel_network_write_16(bytes + 10u, kernel_internet_checksum_finish(kernel_internet_checksum_add(
                                             0u, bytes, KERNEL_INTERNET_PROTOCOL_HEADER_BYTES)));
}
