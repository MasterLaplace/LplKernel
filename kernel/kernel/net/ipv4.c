#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/ipv4.h>

#define KERNEL_INTERNET_PROTOCOL_MORE_FRAGMENTS 0x2000u /**< MF, in the flags and offset field. */
#define KERNEL_INTERNET_PROTOCOL_OFFSET_MASK    0x1FFFu /**< The fragment offset. */

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
    if (kernel_internet_checksum_fold_bytes(bytes, header_bytes) != 0xFFFFu)
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
    const KernelInternetProtocolSubnet_t subnet = kernel_internet_protocol_subnet(address, netmask);

    return kernel_internet_protocol_source_is_acceptable_in((uint8_t) (source >> 24), source, &subnet);
}
