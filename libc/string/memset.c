#include <stdint.h>
#include <string.h>

/** @brief A machine word the fill writes at once, allowed to alias the bytes of any object. */
typedef uint32_t __attribute__((may_alias)) memory_fill_word_t;

/**
 * @brief Sets @p size bytes from @p bufptr to the low byte of @p value.
 *
 * The bytes before the first whole word are set one by one, the words after them one word at a time, and
 * the bytes left one by one. A word costs about as many instructions as a byte did, so a long fill takes a
 * quarter of them.
 *
 * @return @p bufptr.
 */
void *memset(void *bufptr, int value, size_t size)
{
    unsigned char *buf = (unsigned char *) bufptr;
    const unsigned char byte = (unsigned char) value;
    const uintptr_t word_offset_mask = sizeof(memory_fill_word_t) - 1u;

    for (; size != 0u && ((uintptr_t) buf & word_offset_mask) != 0u; --size)
        *buf++ = byte;

    const memory_fill_word_t pattern = (memory_fill_word_t) byte * 0x01010101u;
    memory_fill_word_t *word = (memory_fill_word_t *) buf;

    for (; size >= sizeof(memory_fill_word_t); size -= sizeof(memory_fill_word_t))
        *word++ = pattern;
    buf = (unsigned char *) word;
    for (size_t i = 0u; i < size; ++i)
        buf[i] = byte;
    return bufptr;
}
