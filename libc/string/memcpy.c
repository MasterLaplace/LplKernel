#include <stdint.h>
#include <string.h>

/** @brief A machine word the copy moves at once, allowed to alias the bytes of any object. */
typedef uint32_t __attribute__((may_alias)) memory_copy_word_t;

/**
 * @brief Copies @p size bytes from @p srcptr to @p dstptr, which do not overlap.
 *
 * When both addresses sit at the same offset within a word, the bytes before the first whole word
 * are copied one by one, the words after them one word at a time, and the bytes left one by one;
 * otherwise every byte is copied on its own. A word costs about as many instructions as a byte did,
 * so the engine's double buffers, which copy 24 KiB a server tick, take a quarter of them.
 *
 * @return @p dstptr.
 */
void *memcpy(void *restrict dstptr, const void *restrict srcptr, size_t size)
{
    unsigned char *dst = (unsigned char *) dstptr;
    const unsigned char *src = (const unsigned char *) srcptr;
    const uintptr_t word_offset_mask = sizeof(memory_copy_word_t) - 1u;

    if ((((uintptr_t) dst ^ (uintptr_t) src) & word_offset_mask) == 0u)
    {
        for (; size != 0u && ((uintptr_t) dst & word_offset_mask) != 0u; --size)
            *dst++ = *src++;

        memory_copy_word_t *dst_word = (memory_copy_word_t *) dst;
        const memory_copy_word_t *src_word = (const memory_copy_word_t *) src;

        for (; size >= sizeof(memory_copy_word_t); size -= sizeof(memory_copy_word_t))
            *dst_word++ = *src_word++;
        dst = (unsigned char *) dst_word;
        src = (const unsigned char *) src_word;
    }
    for (size_t i = 0u; i < size; ++i)
        dst[i] = src[i];
    return dstptr;
}
