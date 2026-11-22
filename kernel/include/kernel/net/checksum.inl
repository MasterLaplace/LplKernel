#include <stdint.h>

static inline uint16_t kernel_internet_checksum_finish(uint32_t sum)
{
    sum = (sum & 0xFFFFu) + (sum >> 16);
    sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t) ~sum;
}
