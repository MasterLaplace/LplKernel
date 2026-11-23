#include <kernel/net/checksum.h>

uint32_t kernel_internet_checksum_add(uint32_t sum, const uint8_t *bytes, uint32_t length)
{
    const uint8_t *const end = bytes + (length & ~1u);

    for (; bytes != end; bytes += 2)
        sum += (uint32_t) bytes[0] << 8 | (uint32_t) bytes[1];
    if ((length & 1u) != 0u)
        sum += (uint32_t) bytes[0] << 8;
    return sum;
}

uint16_t kernel_internet_checksum_finish(uint32_t sum)
{
    sum = (sum & 0xFFFFu) + (sum >> 16);
    sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t) ~sum;
}
