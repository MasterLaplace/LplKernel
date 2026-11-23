#include <kernel/net/byte_order.h>
#include <kernel/net/ethernet.h>

#include <string.h>

/**
 * @brief Whether @p address is the broadcast address, every bit set.
 */
static bool kernel_ethernet_is_broadcast(const uint8_t *address)
{
    for (uint32_t byte = 0u; byte < KERNEL_NETWORK_ADDRESS_BYTES; ++byte)
        if (address[byte] != 0xFFu)
            return false;
    return true;
}

bool kernel_ethernet_parse(const uint8_t *bytes, uint32_t length, KernelEthernetFrame_t *out)
{
    if (length < KERNEL_ETHERNET_HEADER_BYTES)
        return false;
    out->destination = bytes;
    out->source = bytes + KERNEL_NETWORK_ADDRESS_BYTES;
    out->type = kernel_network_read_16(bytes + 2u * KERNEL_NETWORK_ADDRESS_BYTES);
    out->payload = bytes + KERNEL_ETHERNET_HEADER_BYTES;
    out->payload_bytes = length - KERNEL_ETHERNET_HEADER_BYTES;
    return true;
}

bool kernel_ethernet_is_for(const KernelEthernetFrame_t *frame, const uint8_t *address)
{
    return memcmp(frame->destination, address, KERNEL_NETWORK_ADDRESS_BYTES) == 0 ||
           kernel_ethernet_is_broadcast(frame->destination);
}

void kernel_ethernet_write_header(uint8_t *bytes, const uint8_t *destination, const uint8_t *source, uint16_t type)
{
    memcpy(bytes, destination, KERNEL_NETWORK_ADDRESS_BYTES);
    memcpy(bytes + KERNEL_NETWORK_ADDRESS_BYTES, source, KERNEL_NETWORK_ADDRESS_BYTES);
    kernel_network_write_16(bytes + 2u * KERNEL_NETWORK_ADDRESS_BYTES, type);
}
