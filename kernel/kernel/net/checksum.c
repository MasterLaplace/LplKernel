#include <kernel/net/byte_order.h>
#include <kernel/net/checksum.h>

/**
 * @brief The byte at @p byte as it counts in a sum of words in the host's own order, when it is the last of an odd
 *        length: the byte at a word's lower address, its other byte zero, whichever end the host holds it at.
 */
static uint32_t kernel_internet_checksum_odd_byte(const uint8_t *byte)
{
    uint16_t word = 0u;

    __builtin_memcpy(&word, byte, 1u);
    return word;
}

/**
 * @brief @p folded, a 16-bit sum of words in the host's own order, as the same sum of big-endian words: its two bytes
 *        stored as the host holds them, then read big-endian.
 */
static uint16_t kernel_internet_checksum_to_big_endian(uint32_t folded)
{
    const uint16_t native = (uint16_t) folded;
    uint8_t bytes[2];

    __builtin_memcpy(bytes, &native, sizeof(native));
    return kernel_network_read_16(bytes);
}

uint32_t kernel_internet_checksum_add(uint32_t sum, const uint8_t *bytes, uint32_t length)
{
    for (; length >= 4u; length -= 4u, bytes += 4u)
    {
        const uint32_t words = kernel_network_read_32(bytes);

        sum += (words >> 16) + (words & 0xFFFFu);
    }
    if (length >= 2u)
    {
        sum += kernel_network_read_16(bytes);
        bytes += 2u;
        length -= 2u;
    }
    if (length != 0u)
        sum += (uint32_t) bytes[0] << 8;
    return sum;
}

uint16_t kernel_internet_checksum_fold_bytes(const uint8_t *bytes, uint32_t length)
{
    uint64_t sum = 0u;
    uint32_t folded = 0u;

    for (; length >= 4u; length -= 4u, bytes += 4u)
    {
        uint32_t words = 0u;

        __builtin_memcpy(&words, bytes, sizeof(words));
        sum += words;
    }
    if (length >= 2u)
    {
        uint16_t word = 0u;

        __builtin_memcpy(&word, bytes, sizeof(word));
        sum += word;
        bytes += 2u;
        length -= 2u;
    }
    if (length != 0u)
        sum += kernel_internet_checksum_odd_byte(bytes);
    sum = (sum & 0xFFFFFFFFu) + (sum >> 32);
    folded = (uint32_t) ((sum & 0xFFFFFFFFu) + (sum >> 32));
    folded = (folded & 0xFFFFu) + (folded >> 16);
    folded = (folded & 0xFFFFu) + (folded >> 16);
    return kernel_internet_checksum_to_big_endian(folded);
}
