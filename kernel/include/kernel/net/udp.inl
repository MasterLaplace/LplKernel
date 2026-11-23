#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>

#include <stdint.h>

static inline uint32_t kernel_user_datagram_header_sum(uint16_t source_port, uint16_t destination_port,
                                                       uint32_t datagram_bytes)
{
    return (uint32_t) source_port + (uint32_t) destination_port + datagram_bytes;
}

static inline uint32_t kernel_user_datagram_pseudo_header_sum(uint32_t source, uint32_t destination,
                                                              uint32_t datagram_bytes)
{
    return (source >> 16) + (source & 0xFFFFu) + (destination >> 16) + (destination & 0xFFFFu) +
           KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM + datagram_bytes;
}

static inline uint16_t kernel_user_datagram_checksum(uint32_t pseudo_header_sum, uint16_t source_port,
                                                     uint16_t destination_port, uint32_t payload_bytes,
                                                     uint32_t payload_sum)
{
    const uint32_t datagram_bytes = KERNEL_USER_DATAGRAM_HEADER_BYTES + payload_bytes;
    const uint16_t checksum = kernel_internet_checksum_finish(
        pseudo_header_sum + kernel_user_datagram_header_sum(source_port, destination_port, datagram_bytes) +
        payload_sum);

    return checksum == 0u ? 0xFFFFu : checksum;
}

static inline void kernel_user_datagram_write_header(uint8_t *bytes, uint16_t source_port, uint16_t destination_port,
                                                     uint32_t payload_bytes, uint16_t checksum)
{
    kernel_network_write_16(bytes, source_port);
    kernel_network_write_16(bytes + 2u, destination_port);
    kernel_network_write_16(bytes + 4u, (uint16_t) (KERNEL_USER_DATAGRAM_HEADER_BYTES + payload_bytes));
    kernel_network_write_16(bytes + 6u, checksum);
}
