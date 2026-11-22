#include <stdbool.h>
#include <stdint.h>

static inline bool kernel_ethernet_is_group(const uint8_t *station) { return (station[0] & 0x01u) != 0u; }
