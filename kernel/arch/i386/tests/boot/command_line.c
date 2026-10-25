#include <kernel/boot/boot_module.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(boot_command_line, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief Whether @p copy holds exactly the first @p length characters of @p text, then its NUL.
 */
static bool boot_command_line_test_holds(const char *copy, const char *text, uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index)
        if (copy[index] != text[index])
            return false;
    return copy[length] == '\0';
}

/**
 * @brief A line longer than its copy keeps its head, and the copy counts what it left out: the count
 *        is what tells a run that its selection may have lost its tail.
 */
KERNEL_TEST(a_line_longer_than_its_copy_keeps_its_head_and_counts_the_rest)
{
    static const char line[] = "lpl.test=cpu_topology.*";
    char copy[8];

    kernel_test_check(test, boot_command_line_copy(copy, sizeof(copy), line) == 16u,
                      "the copy left out the 16 characters past its 7");
    kernel_test_check(test, boot_command_line_test_holds(copy, line, 7u), "the copy holds the line's first 7");
}

/**
 * @brief A line that fills the copy to its last byte is whole, and one more character is one dropped.
 */
KERNEL_TEST(a_line_that_fits_its_copy_is_whole)
{
    char copy[8];

    kernel_test_check(test, boot_command_line_copy(copy, sizeof(copy), "lpl.tes") == 0u,
                      "seven characters fit a copy of eight bytes");
    kernel_test_check(test, boot_command_line_test_holds(copy, "lpl.tes", 7u), "the copy holds all seven");
    kernel_test_check(test, boot_command_line_copy(copy, sizeof(copy), "lpl.test") == 1u,
                      "an eighth character is one dropped");
    kernel_test_check(test, boot_command_line_copy(copy, 1u, "lpl") == 3u && copy[0] == '\0',
                      "a copy of one byte holds the NUL alone");
}
