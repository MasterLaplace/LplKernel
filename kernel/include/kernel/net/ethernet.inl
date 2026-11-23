#include <kernel/net/byte_order.h>

#include <stdbool.h>
#include <stdint.h>

static inline bool kernel_ethernet_is_group(const uint8_t *station) { return (station[0] & 0x01u) != 0u; }

static inline void kernel_ethernet_write_header(uint8_t *bytes, const uint8_t *destination, const uint8_t *source,
                                                uint16_t type)
{
    kernel_network_station_copy(bytes, destination);
    kernel_network_station_copy(bytes + KERNEL_NETWORK_ADDRESS_BYTES, source);
    kernel_network_write_16(bytes + 2u * KERNEL_NETWORK_ADDRESS_BYTES, type);
}
