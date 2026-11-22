#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/udp.h>

/**
 * @brief The sum of a datagram's header words but its checksum.
 */
static uint32_t kernel_user_datagram_header_sum(uint16_t source_port, uint16_t destination_port,
                                                uint32_t datagram_bytes)
{
    return (uint32_t) source_port + (uint32_t) destination_port + datagram_bytes;
}

uint32_t kernel_user_datagram_pseudo_header_sum(uint32_t source, uint32_t destination, uint32_t datagram_bytes)
{
    return (source >> 16) + (source & 0xFFFFu) + (destination >> 16) + (destination & 0xFFFFu) +
           KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM + datagram_bytes;
}

KernelUserDatagramStatus_t kernel_user_datagram_parse(const KernelInternetProtocolPacket_t *packet,
                                                      KernelUserDatagram_t *out)
{
    if (packet->payload_bytes < KERNEL_USER_DATAGRAM_HEADER_BYTES)
        return KERNEL_USER_DATAGRAM_TRUNCATED;

    const uint8_t *const bytes = packet->payload;
    const uint32_t datagram_bytes = kernel_network_read_16(bytes + 4u);

    if (datagram_bytes < KERNEL_USER_DATAGRAM_HEADER_BYTES || datagram_bytes > packet->payload_bytes)
        return KERNEL_USER_DATAGRAM_BAD_LENGTH;

    const uint16_t source_port = kernel_network_read_16(bytes);
    const uint16_t destination_port = kernel_network_read_16(bytes + 2u);
    const uint16_t checksum = kernel_network_read_16(bytes + 6u);
    const uint32_t payload_bytes = datagram_bytes - KERNEL_USER_DATAGRAM_HEADER_BYTES;
    const uint32_t payload_sum =
        kernel_internet_checksum_add(0u, bytes + KERNEL_USER_DATAGRAM_HEADER_BYTES, payload_bytes);

    if (checksum != 0u &&
        kernel_internet_checksum_finish(
            kernel_user_datagram_pseudo_header_sum(packet->source, packet->destination, datagram_bytes) +
            kernel_user_datagram_header_sum(source_port, destination_port, datagram_bytes) + checksum + payload_sum) !=
            0u)
        return KERNEL_USER_DATAGRAM_BAD_CHECKSUM;
    out->source_port = source_port;
    out->destination_port = destination_port;
    out->payload = bytes + KERNEL_USER_DATAGRAM_HEADER_BYTES;
    out->payload_bytes = payload_bytes;
    out->payload_sum = payload_sum;
    return checksum == 0u ? KERNEL_USER_DATAGRAM_NO_CHECKSUM : KERNEL_USER_DATAGRAM_TAKEN;
}

uint16_t kernel_user_datagram_checksum(uint32_t pseudo_header_sum, uint16_t source_port, uint16_t destination_port,
                                       uint32_t payload_bytes, uint32_t payload_sum)
{
    const uint32_t datagram_bytes = KERNEL_USER_DATAGRAM_HEADER_BYTES + payload_bytes;
    const uint16_t checksum = kernel_internet_checksum_finish(
        pseudo_header_sum + kernel_user_datagram_header_sum(source_port, destination_port, datagram_bytes) +
        payload_sum);

    return checksum == 0u ? 0xFFFFu : checksum;
}

void kernel_user_datagram_write_header(uint8_t *bytes, uint16_t source_port, uint16_t destination_port,
                                       uint32_t payload_bytes, uint16_t checksum)
{
    kernel_network_write_16(bytes, source_port);
    kernel_network_write_16(bytes + 2u, destination_port);
    kernel_network_write_16(bytes + 4u, (uint16_t) (KERNEL_USER_DATAGRAM_HEADER_BYTES + payload_bytes));
    kernel_network_write_16(bytes + 6u, checksum);
}
