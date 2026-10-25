#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(test_runner, KERNEL_TEST_STAGE_BOOTED);

static const char test_runner_suite[] = "boot_command_line";
static const char test_runner_name[] =
    "the_boot_line_that_selects_this_test_by_its_full_name_of_more_than_one_hundred_and_twenty_seven_characters_"
    "reaches_it_whole";

/**
 * @brief Whether the first @p length characters of @p pattern name the made-up test of 141 characters.
 */
static bool test_runner_names(const char *pattern, uint32_t length)
{
    return kernel_test_name_matches(pattern, pattern + length, test_runner_suite, test_runner_name);
}

/**
 * @brief A pattern names a test by its whole "suite.name" however long, here 141 characters, and its first 140
 *        alone name nothing: the runner once cut a name at 127 characters, and a pattern naming a longer test in
 *        full matched nothing.
 */
KERNEL_TEST(a_pattern_names_a_test_by_its_whole_name_however_long)
{
    static const char whole[] = "boot_command_line.the_boot_line_that_selects_this_test_by_its_full_name_of_more_than_"
                                "one_hundred_and_twenty_seven_characters_reaches_it_whole";

    kernel_test_measure(test, "length", (uint32_t) sizeof(whole) - 1u);
    kernel_test_check(test, test_runner_names(whole, sizeof(whole) - 1u), "the whole name names the test");
    kernel_test_check(test, !test_runner_names(whole, sizeof(whole) - 2u), "the name less its last character does not");
    kernel_test_check(test, test_runner_names("*reaches_it_whole", 17u), "a star then the tail past 127 names it");
    kernel_test_check(test, test_runner_names("boot_command_line.*", 19u), "the suite then a star names it");
    kernel_test_check(test, !test_runner_names("boot_command_line", 17u), "the suite alone does not");
}

/**
 * @brief A star matches any run, an empty one included, and the match backtracks past a false start.
 */
KERNEL_TEST(a_star_matches_any_run_and_backtracks)
{
    kernel_test_check(test, kernel_test_name_matches("a*b.c", "a*b.c" + 5, "ab", "c"), "a star matches nothing");
    kernel_test_check(test, kernel_test_name_matches("*b.c", "*b.c" + 4, "abab", "c"),
                      "a star backtracks past the first b");
    kernel_test_check(test, !kernel_test_name_matches("*b.d", "*b.d" + 4, "abab", "c"), "a wrong tail fails");
    kernel_test_check(test, kernel_test_name_matches("*", "*" + 1, "", ""), "a star matches the name of nothing");
}
