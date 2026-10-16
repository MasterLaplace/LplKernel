#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(memory_copies, KERNEL_TEST_STAGE_INITIALIZATION);

/** @brief Bytes each buffer holds: room for every offset and length the copy test tries, with margins. */
#define MEMORY_COPIES_BUFFER_SIZE 96u

/** @brief Lengths copied: none, the bytes under a word, a few words, and enough for a long run of them. */
#define MEMORY_COPIES_LONGEST 67u

/** @brief The byte a copy leaves in place: anything the copy writes outside its range shows. */
#define MEMORY_COPIES_UNTOUCHED 0x00u

/** @brief The byte the source holds at @p index: each one different, and none of them the untouched one. */
static uint8_t memory_copies_source_byte(uint32_t index) { return (uint8_t) (index + 1u); }

/**
 * @brief Copies @p size bytes from @p source_offset of a source to @p destination_offset of a
 *        destination, and checks the range holds the source and every byte around it is untouched.
 */
static bool memory_copies_copy_and_compare(uint32_t destination_offset, uint32_t source_offset, uint32_t size)
{
    uint32_t source_words[MEMORY_COPIES_BUFFER_SIZE / sizeof(uint32_t)];
    uint32_t destination_words[MEMORY_COPIES_BUFFER_SIZE / sizeof(uint32_t)];
    uint8_t *source = (uint8_t *) source_words;
    uint8_t *destination = (uint8_t *) destination_words;

    for (uint32_t index = 0u; index < MEMORY_COPIES_BUFFER_SIZE; ++index)
    {
        source[index] = memory_copies_source_byte(index);
        destination[index] = MEMORY_COPIES_UNTOUCHED;
    }
    if (memcpy(destination + destination_offset, source + source_offset, size) != destination + destination_offset)
        return false;
    for (uint32_t index = 0u; index < MEMORY_COPIES_BUFFER_SIZE; ++index)
    {
        const bool copied = index >= destination_offset && index < destination_offset + size;
        const uint8_t expected =
            copied ? memory_copies_source_byte(index - destination_offset + source_offset) : MEMORY_COPIES_UNTOUCHED;

        if (destination[index] != expected)
            return false;
    }
    return true;
}

/**
 * @brief memcpy copies every length from none to 67 bytes, from and to every offset within a word,
 *        and writes nothing outside the bytes it was given.
 *
 * Equal offsets take the path that copies whole words, unequal ones the path that copies bytes; the
 * lengths cover the bytes before the first word, the words, and the bytes after the last.
 */
KERNEL_TEST(copies_every_length_at_every_offset)
{
    uint32_t copies = 0u;
    uint32_t mismatches = 0u;
    uint32_t aligned_copies = 0u;

    for (uint32_t destination_offset = 0u; destination_offset < sizeof(uint32_t); ++destination_offset)
    {
        for (uint32_t source_offset = 0u; source_offset < sizeof(uint32_t); ++source_offset)
        {
            for (uint32_t size = 0u; size <= MEMORY_COPIES_LONGEST; ++size)
            {
                if (!memory_copies_copy_and_compare(destination_offset, source_offset, size))
                    ++mismatches;
                ++copies;
                if (destination_offset == source_offset)
                    ++aligned_copies;
            }
        }
    }

    kernel_test_check(test, mismatches == 0u, "every copy holds the source and leaves the bytes around it untouched");
    kernel_test_check(test, aligned_copies != 0u && aligned_copies != copies,
                      "both the word path and the byte path were taken");
    kernel_test_measure(test, "copies", copies);
    kernel_test_measure(test, "copies_by_words", aligned_copies);
}
