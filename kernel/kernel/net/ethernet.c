#include <kernel/net/byte_order.h>
#include <kernel/net/ethernet.h>

const uint8_t kernel_ethernet_broadcast_station[KERNEL_NETWORK_ADDRESS_BYTES] = {0xFFu, 0xFFu, 0xFFu,
                                                                                 0xFFu, 0xFFu, 0xFFu};

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
    return kernel_network_station_equals(frame->destination, address) ||
           kernel_network_station_equals(frame->destination, kernel_ethernet_broadcast_station);
}
