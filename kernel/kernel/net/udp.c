#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>
#include <kernel/net/udp.h>

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
        kernel_internet_checksum_fold_bytes(bytes + KERNEL_USER_DATAGRAM_HEADER_BYTES, payload_bytes);

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
