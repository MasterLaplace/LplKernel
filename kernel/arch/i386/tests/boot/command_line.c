#include <kernel/boot/boot_module.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(boot_command_line, KERNEL_TEST_STAGE_BOOTED);

static char boot_command_line_test_line[BOOT_COMMAND_LINE_CAPACITY + 1u];
static char boot_command_line_test_copy[BOOT_COMMAND_LINE_CAPACITY];

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
 * @brief Fills the test line with @p length characters, then its NUL.
 */
static const char *boot_command_line_test_line_of(uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index)
        boot_command_line_test_line[index] = (char) ('a' + index % 26u);
    boot_command_line_test_line[length] = '\0';
    return boot_command_line_test_line;
}

/**
 * @brief A line longer than its copy keeps its head, and the copy counts what it left out: the count
 *        is what tells a run that its selection may have lost its tail.
 */
KERNEL_TEST(a_line_longer_than_its_copy_keeps_its_head_and_counts_the_rest)
{
    static const char line[] = "lpl.test=cpu_topology.*";
    char copy[8];

    kernel_test_check(test, boot_command_line_copy(copy, sizeof(copy), line, BOOT_COMMAND_LINE_LONGEST) == 16u,
                      "the copy left out the 16 characters past its 7");
    kernel_test_check(test, boot_command_line_test_holds(copy, line, 7u), "the copy holds the line's first 7");
}

/**
 * @brief A copy holds as many characters as its bytes leave room for beside the NUL: seven in eight
 *        bytes, an eighth dropped, none in one byte, and nothing written in none.
 */
KERNEL_TEST(a_copy_holds_what_its_bytes_leave_room_for_beside_the_nul)
{
    char copy[8];

    kernel_test_check(test, boot_command_line_copy(copy, sizeof(copy), "lpl.tes", BOOT_COMMAND_LINE_LONGEST) == 0u,
                      "seven characters fit a copy of eight bytes");
    kernel_test_check(test, boot_command_line_test_holds(copy, "lpl.tes", 7u), "the copy holds all seven");
    kernel_test_check(test, boot_command_line_copy(copy, sizeof(copy), "lpl.test", BOOT_COMMAND_LINE_LONGEST) == 1u,
                      "an eighth character is one dropped");
    kernel_test_check(test, boot_command_line_copy(copy, 1u, "lpl", BOOT_COMMAND_LINE_LONGEST) == 3u && copy[0] == '\0',
                      "a copy of one byte holds the NUL alone");
    copy[0] = 'x';
    kernel_test_check(test, boot_command_line_copy(copy, 0u, "lpl", BOOT_COMMAND_LINE_LONGEST) == 3u && copy[0] == 'x',
                      "a copy of no byte drops the three characters and writes nothing");
}

/**
 * @brief A line is read no further than its longest: one with no NUL within it counts as that long.
 */
KERNEL_TEST(a_line_is_read_no_further_than_its_longest)
{
    char copy[8];

    kernel_test_check(test, boot_command_line_copy(copy, sizeof(copy), "lpl.test=heap.*", 10u) == 3u,
                      "a 15-character line read up to 10 counts 10, and drops the 3 past the copy's 7");
    kernel_test_check(test, boot_command_line_test_holds(copy, "lpl.test=heap.*", 7u), "the copy holds its first 7");
}

/**
 * @brief The kernel keeps a boot line of 1,023 characters whole, and drops the 1,024th: QEMU puts the
 *        kernel's path in front of the `-append` text, and the ACPI selections pass 255 characters.
 */
KERNEL_TEST(the_kernel_keeps_a_line_of_1023_characters)
{
    const char *line = boot_command_line_test_line_of(1023u);

    kernel_test_check(test,
                      boot_command_line_copy(boot_command_line_test_copy, BOOT_COMMAND_LINE_CAPACITY, line,
                                             BOOT_COMMAND_LINE_LONGEST) == 0u &&
                          boot_command_line_test_holds(boot_command_line_test_copy, line, 1023u),
                      "a line of 1,023 characters fits the kernel's copy whole");
    line = boot_command_line_test_line_of(1024u);
    kernel_test_check(test,
                      boot_command_line_copy(boot_command_line_test_copy, BOOT_COMMAND_LINE_CAPACITY, line,
                                             BOOT_COMMAND_LINE_LONGEST) == 1u,
                      "the 1,024th character is dropped");
}
