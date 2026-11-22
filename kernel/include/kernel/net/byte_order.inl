#include <stdint.h>

static inline uint16_t kernel_network_read_16(const uint8_t *bytes)
{
    return (uint16_t) ((uint32_t) bytes[0] << 8 | (uint32_t) bytes[1]);
}

static inline uint32_t kernel_network_read_32(const uint8_t *bytes)
{
    return (uint32_t) bytes[0] << 24 | (uint32_t) bytes[1] << 16 | (uint32_t) bytes[2] << 8 | (uint32_t) bytes[3];
}

static inline void kernel_network_write_16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t) (value >> 8);
    bytes[1] = (uint8_t) value;
}

static inline void kernel_network_write_32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t) (value >> 24);
    bytes[1] = (uint8_t) (value >> 16);
    bytes[2] = (uint8_t) (value >> 8);
    bytes[3] = (uint8_t) value;
}
