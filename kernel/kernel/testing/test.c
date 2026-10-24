#include <kernel/boot/boot_module.h>
#include <kernel/testing/test.h>

#if !defined(LPL_PLUGIN_UNAVAILABLE)
#    include <libengine/libengine.h>
#endif

/**
 * @brief Bounds of the `.kernel_tests` section, which holds one pointer per declared test.
 */
extern const KernelTestCase_t *const _kernel_tests_start[];
extern const KernelTestCase_t *const _kernel_tests_end[];

/** The boot option that selects tests by name. */
#define KERNEL_TEST_SELECTION_OPTION "lpl.test="

/** The boot option that names the tests a boot must run: one of them skipped fails. */
#define KERNEL_TEST_REQUIRED_OPTION "lpl.test.require="

/** Patterns the required option may hold: one bit each in kernel_test_required_named. */
#define KERNEL_TEST_MOST_REQUIRED_PATTERNS 32u

/** Longest "suite.test" a selection pattern is matched against; a longer name is cut there. */
#define KERNEL_TEST_QUALIFIED_NAME_CAPACITY 128u

/** Indentation of a line that belongs to a suite's subtest. */
#define KERNEL_TEST_SUBTEST_INDENT "    "

/** What became of one test. */
typedef enum KernelTestOutcome {
    KERNEL_TEST_OUTCOME_PASSED,
    KERNEL_TEST_OUTCOME_SKIPPED,
    KERNEL_TEST_OUTCOME_FAILED,
} KernelTestOutcome_t;

struct KernelTest {
    const KernelTestCase_t *test_case; /**< The test being run. */
    Serial_t *serial;                  /**< Port its report is written to. */
    uint32_t checks;                   /**< Claims it checked. */
    uint32_t failures;                 /**< Claims that did not hold. */
    const char *skip_reason;           /**< Why it did not run, or NULL. */
};

static bool kernel_test_header_written = false;
static uint32_t kernel_test_next_suite_number = 1u;
static uint32_t kernel_test_passed_count = 0u;
static uint32_t kernel_test_failed_count = 0u;
static uint32_t kernel_test_skipped_count = 0u;
static uint32_t kernel_test_check_count = 0u;
static uint32_t kernel_test_selected_count = 0u;
static uint32_t kernel_test_required_named = 0u;

static int32_t kernel_test_compare_strings(const char *lhs, const char *rhs)
{
    while (*lhs != '\0' && *lhs == *rhs)
    {
        ++lhs;
        ++rhs;
    }
    return (int32_t) (uint8_t) *lhs - (int32_t) (uint8_t) *rhs;
}

/**
 * @brief The order the runner walks the tests in: stage, then file, then line.
 *
 * @details It does not depend on where the compiler and the linker placed the entries, so the
 *          shell build and the xmake build run the tests in the same order.
 */
static bool kernel_test_comes_before(const KernelTestCase_t *lhs, const KernelTestCase_t *rhs)
{
    if (lhs->suite->stage != rhs->suite->stage)
        return lhs->suite->stage < rhs->suite->stage;

    const int32_t file_order = kernel_test_compare_strings(lhs->file, rhs->file);

    if (file_order != 0)
        return file_order < 0;
    return lhs->line < rhs->line;
}

/**
 * @brief The first test after @p previous in the runner's order, or the first of all when it is NULL.
 *
 * @details A scan rather than a sort: the section is read-only once the kernel is up, and the
 *          scan needs no buffer whose size would cap the number of tests.
 */
static const KernelTestCase_t *kernel_test_after(const KernelTestCase_t *previous)
{
    const KernelTestCase_t *next = NULL;

    for (const KernelTestCase_t *const *entry = _kernel_tests_start; entry < _kernel_tests_end; ++entry)
    {
        const KernelTestCase_t *candidate = *entry;

        if (previous && !kernel_test_comes_before(previous, candidate))
            continue;
        if (!next || kernel_test_comes_before(candidate, next))
            next = candidate;
    }
    return next;
}

static uint32_t kernel_test_count_suites(void)
{
    uint32_t suites = 0u;
    const KernelTestSuite_t *current = NULL;

    for (const KernelTestCase_t *test_case = kernel_test_after(NULL); test_case;
         test_case = kernel_test_after(test_case))
    {
        if (test_case->suite != current)
        {
            current = test_case->suite;
            ++suites;
        }
    }
    return suites;
}

static uint32_t kernel_test_count_suite_tests(const KernelTestCase_t *first)
{
    uint32_t tests = 0u;

    for (const KernelTestCase_t *test_case = first; test_case && test_case->suite == first->suite;
         test_case = kernel_test_after(test_case))
        ++tests;
    return tests;
}

/**
 * @brief Matches @p subject against the pattern [@p pattern, @p pattern_end), where `*` matches any run.
 */
static bool kernel_test_pattern_matches(const char *pattern, const char *pattern_end, const char *subject)
{
    const char *star = NULL;
    const char *resume = NULL;

    while (*subject != '\0')
    {
        if (pattern < pattern_end && *pattern == '*')
        {
            star = pattern++;
            resume = subject;
        }
        else if (pattern < pattern_end && *pattern == *subject)
        {
            ++pattern;
            ++subject;
        }
        else if (star)
        {
            pattern = star + 1;
            subject = ++resume;
        }
        else
        {
            return false;
        }
    }

    while (pattern < pattern_end && *pattern == '*')
        ++pattern;
    return pattern == pattern_end;
}

static void kernel_test_qualified_name(const KernelTestCase_t *test_case, char *buffer)
{
    uint32_t length = 0u;

    for (const char *c = test_case->suite->name; *c != '\0' && length + 1u < KERNEL_TEST_QUALIFIED_NAME_CAPACITY; ++c)
        buffer[length++] = *c;
    if (length + 1u < KERNEL_TEST_QUALIFIED_NAME_CAPACITY)
        buffer[length++] = '.';
    for (const char *c = test_case->name; *c != '\0' && length + 1u < KERNEL_TEST_QUALIFIED_NAME_CAPACITY; ++c)
        buffer[length++] = *c;
    buffer[length] = '\0';
}

static bool kernel_test_pattern_has_a_star(const char *pattern, const char *pattern_end)
{
    for (; pattern < pattern_end; ++pattern)
    {
        if (*pattern == '*')
            return true;
    }
    return false;
}

/**
 * @brief Whether the comma-separated patterns of @p selection, which ends at a space or at the
 *        end of the command line, name @p test_case.
 *
 * @param named_in_full Whether only a pattern without `*` counts, as for a manual test: a pattern
 *                      such as `ring*` must not run a test that halts the machine.
 */
static bool kernel_test_is_selected_by(const char *selection, const KernelTestCase_t *test_case, bool named_in_full)
{
    char qualified_name[KERNEL_TEST_QUALIFIED_NAME_CAPACITY];

    kernel_test_qualified_name(test_case, qualified_name);

    const char *pattern = selection;

    for (;;)
    {
        const char *pattern_end = pattern;

        while (*pattern_end != '\0' && *pattern_end != ',' && *pattern_end != ' ')
            ++pattern_end;
        if (pattern_end > pattern && !(named_in_full && kernel_test_pattern_has_a_star(pattern, pattern_end)) &&
            kernel_test_pattern_matches(pattern, pattern_end, qualified_name))
            return true;
        if (*pattern_end != ',')
            return false;
        pattern = pattern_end + 1;
    }
}

/**
 * @brief The patterns of @p patterns, comma-separated and ending at a space or at the end of the
 *        command line, that name @p test_case: one bit per pattern, from the first, up to
 *        KERNEL_TEST_MOST_REQUIRED_PATTERNS.
 */
static uint32_t kernel_test_patterns_naming(const char *patterns, const KernelTestCase_t *test_case)
{
    char qualified_name[KERNEL_TEST_QUALIFIED_NAME_CAPACITY];
    uint32_t naming = 0u;
    const char *pattern = patterns;

    kernel_test_qualified_name(test_case, qualified_name);
    for (uint32_t index = 0u; index < KERNEL_TEST_MOST_REQUIRED_PATTERNS; ++index)
    {
        const char *pattern_end = pattern;

        while (*pattern_end != '\0' && *pattern_end != ',' && *pattern_end != ' ')
            ++pattern_end;
        if (pattern_end > pattern && kernel_test_pattern_matches(pattern, pattern_end, qualified_name))
            naming |= 1u << index;
        if (*pattern_end != ',')
            break;
        pattern = pattern_end + 1;
    }
    return naming;
}

/**
 * @brief Why @p test_case does not run on this boot, or NULL when it runs.
 */
static const char *kernel_test_reason_not_to_run(const char *selection, const KernelTestCase_t *test_case)
{
    const bool manual = (test_case->manual_reason != NULL);

    if (!selection)
        return test_case->manual_reason;
    if (!kernel_test_is_selected_by(selection, test_case, manual))
        return manual ? test_case->manual_reason : "not selected";
    ++kernel_test_selected_count;
    return NULL;
}

static void kernel_test_write_test_prefix(KernelTest_t *test)
{
    serial_write_string(test->serial, KERNEL_TEST_SUBTEST_INDENT "# ");
    serial_write_string(test->serial, test->test_case->suite->name);
    serial_write_char(test->serial, '.');
    serial_write_string(test->serial, test->test_case->name);
}

/**
 * @brief Writes the KTAP header, whose plan counts the suites of the kernel and of its libraries.
 */
static void kernel_test_write_header(Serial_t *serial)
{
    uint32_t suites = kernel_test_count_suites();

#if !defined(LPL_PLUGIN_UNAVAILABLE)
    suites += libengine_test_suite_count();
#endif
    serial_write_string(serial, "KTAP version 1\n1..");
    serial_write_unsigned(serial, suites);
    serial_write_char(serial, '\n');
    kernel_test_header_written = true;
}

static void kernel_test_write_result(Serial_t *serial, const char *indent, bool passed, uint32_t number,
                                     const char *name, const char *skip_reason)
{
    serial_write_string(serial, indent);
    serial_write_string(serial, passed ? "ok " : "not ok ");
    serial_write_unsigned(serial, number);
    serial_write_char(serial, ' ');
    serial_write_string(serial, name);
    if (skip_reason)
    {
        serial_write_string(serial, " # SKIP ");
        serial_write_string(serial, skip_reason);
    }
    serial_write_char(serial, '\n');
}

/**
 * @brief Runs one test, or reports why it does not run, and writes its result line.
 *
 * @details A test the required option names fails when it does not run, for whatever reason: not
 *          selected, manual, or skipped by its body, with that reason written first.
 */
static KernelTestOutcome_t kernel_test_run_one(const KernelTestCase_t *test_case, uint32_t number,
                                               const char *selection, const char *required, Serial_t *serial)
{
    KernelTest_t test = {test_case, serial, 0u, 0u, kernel_test_reason_not_to_run(selection, test_case)};
    const uint32_t required_by = required ? kernel_test_patterns_naming(required, test_case) : 0u;

    kernel_test_required_named |= required_by;

    if (!test.skip_reason)
    {
        test_case->body(&test);
        kernel_test_check_count += test.checks;
        if (!test.skip_reason && test.checks == 0u)
        {
            kernel_test_write_test_prefix(&test);
            serial_write_string(serial, ": checked nothing, so it could not have failed\n");
            ++test.failures;
        }
    }
    if (required_by != 0u && test.skip_reason)
    {
        kernel_test_note(&test, test.skip_reason);
        ++kernel_test_check_count;
        (void) kernel_test_check(&test, false, "a required test runs");
    }

    const bool passed = (test.failures == 0u);

    kernel_test_write_result(serial, KERNEL_TEST_SUBTEST_INDENT, passed, number, test_case->name,
                             passed ? test.skip_reason : NULL);
    if (!passed)
    {
        ++kernel_test_failed_count;
        return KERNEL_TEST_OUTCOME_FAILED;
    }
    if (test.skip_reason)
    {
        ++kernel_test_skipped_count;
        return KERNEL_TEST_OUTCOME_SKIPPED;
    }
    ++kernel_test_passed_count;
    return KERNEL_TEST_OUTCOME_PASSED;
}

/**
 * @brief Runs the suite that starts at @p first as one KTAP subtest.
 *
 * @return The first test of the next suite, or NULL after the last.
 */
static const KernelTestCase_t *kernel_test_run_suite(const KernelTestCase_t *first, const char *selection,
                                                     const char *required, Serial_t *serial)
{
    const KernelTestSuite_t *suite = first->suite;
    uint32_t number = 0u;
    uint32_t failed = 0u;
    uint32_t skipped = 0u;
    const KernelTestCase_t *test_case = first;

    serial_write_string(serial, KERNEL_TEST_SUBTEST_INDENT "KTAP version 1\n" KERNEL_TEST_SUBTEST_INDENT "# Subtest: ");
    serial_write_string(serial, suite->name);
    serial_write_string(serial, "\n" KERNEL_TEST_SUBTEST_INDENT "1..");
    serial_write_unsigned(serial, kernel_test_count_suite_tests(first));
    serial_write_char(serial, '\n');

    for (; test_case && test_case->suite == suite; test_case = kernel_test_after(test_case))
    {
        const KernelTestOutcome_t outcome = kernel_test_run_one(test_case, ++number, selection, required, serial);

        failed += (outcome == KERNEL_TEST_OUTCOME_FAILED) ? 1u : 0u;
        skipped += (outcome == KERNEL_TEST_OUTCOME_SKIPPED) ? 1u : 0u;
    }

    kernel_test_write_result(serial, "", failed == 0u, kernel_test_next_suite_number++, suite->name,
                             (skipped == number) ? "every test skipped" : NULL);
    return test_case;
}

#if !defined(LPL_PLUGIN_UNAVAILABLE)
static void kernel_test_write_text(void *serial, const char *text, size_t length)
{
    for (size_t index = 0u; index < length; ++index)
        serial_write_char((Serial_t *) serial, text[index]);
}

/**
 * @brief Runs the LPL_TEST tests of the linked libraries after the kernel's, as further suites of
 *        the same report, and adds what they came to to the kernel's totals.
 */
static void kernel_test_run_libraries(Serial_t *serial, const char *selection, const char *required)
{
    const libengine_test_totals_t engine =
        libengine_test_run(kernel_test_write_text, serial, selection, required, kernel_test_next_suite_number);

    kernel_test_next_suite_number += libengine_test_suite_count();
    kernel_test_passed_count += engine.passed;
    kernel_test_failed_count += engine.failed;
    kernel_test_skipped_count += engine.skipped;
    kernel_test_check_count += engine.checks;
    kernel_test_selected_count += engine.selected;
    kernel_test_required_named |= engine.required;
}
#endif

/**
 * @brief Writes a line for every pattern of the required option that named no test, kernel's or
 *        libraries', and counts each as a failed test: a pattern that names nothing requires
 *        nothing, and is a typo or a test renamed since.
 */
static void kernel_test_write_unnamed_required(Serial_t *serial, const char *required)
{
    const char *pattern = required;

    for (uint32_t index = 0u; required && index < KERNEL_TEST_MOST_REQUIRED_PATTERNS; ++index)
    {
        const char *pattern_end = pattern;

        while (*pattern_end != '\0' && *pattern_end != ',' && *pattern_end != ' ')
            ++pattern_end;
        if (pattern_end > pattern && (kernel_test_required_named & (1u << index)) == 0u)
        {
            serial_write_string(serial, "# " KERNEL_TEST_REQUIRED_OPTION " the pattern ");
            for (const char *c = pattern; c < pattern_end; ++c)
                serial_write_char(serial, *c);
            serial_write_string(serial, " named no test\n");
            ++kernel_test_failed_count;
        }
        if (*pattern_end != ',')
            break;
        pattern = pattern_end + 1;
    }
}

static void kernel_test_write_totals(Serial_t *serial, const char *selection, const char *required)
{
    if (selection && kernel_test_selected_count == 0u)
        serial_write_string(serial, "# " KERNEL_TEST_SELECTION_OPTION " selected no test\n");
    kernel_test_write_unnamed_required(serial, required);
    serial_write_string(serial, "# Totals: pass:");
    serial_write_unsigned(serial, kernel_test_passed_count);
    serial_write_string(serial, " fail:");
    serial_write_unsigned(serial, kernel_test_failed_count);
    serial_write_string(serial, " skip:");
    serial_write_unsigned(serial, kernel_test_skipped_count);
    serial_write_string(serial, " checks:");
    serial_write_unsigned(serial, kernel_test_check_count);
    serial_write_char(serial, '\n');
}

bool kernel_test_check(KernelTest_t *test, bool condition, const char *claim)
{
    ++test->checks;
    if (condition)
        return true;

    ++test->failures;
    kernel_test_write_test_prefix(test);
    serial_write_string(test->serial, ": does not hold: ");
    serial_write_string(test->serial, claim);
    serial_write_char(test->serial, '\n');
    return false;
}

void kernel_test_skip(KernelTest_t *test, const char *reason) { test->skip_reason = reason; }

void kernel_test_measure(KernelTest_t *test, const char *key, uint32_t value)
{
    kernel_test_write_test_prefix(test);
    serial_write_string(test->serial, ": ");
    serial_write_string(test->serial, key);
    serial_write_char(test->serial, '=');
    serial_write_unsigned(test->serial, value);
    serial_write_char(test->serial, '\n');
}

void kernel_test_measure_hexadecimal(KernelTest_t *test, const char *key, uint32_t value)
{
    kernel_test_write_test_prefix(test);
    serial_write_string(test->serial, ": ");
    serial_write_string(test->serial, key);
    serial_write_char(test->serial, '=');
    serial_write_hex32(test->serial, value);
    serial_write_char(test->serial, '\n');
}

void kernel_test_note(KernelTest_t *test, const char *text)
{
    kernel_test_write_test_prefix(test);
    serial_write_string(test->serial, ": ");
    serial_write_string(test->serial, text);
    serial_write_char(test->serial, '\n');
}

void kernel_test_run_stage(KernelTestStage_t stage, Serial_t *serial)
{
    if (!kernel_test_header_written)
        kernel_test_write_header(serial);

    const char *selection = boot_command_line_option(KERNEL_TEST_SELECTION_OPTION);
    const char *required = boot_command_line_option(KERNEL_TEST_REQUIRED_OPTION);
    const KernelTestCase_t *test_case = kernel_test_after(NULL);

    while (test_case && test_case->suite->stage != stage)
        test_case = kernel_test_after(test_case);
    while (test_case && test_case->suite->stage == stage)
        test_case = kernel_test_run_suite(test_case, selection, required, serial);

    if (stage == KERNEL_TEST_STAGE_PUBLISHED)
        kernel_test_write_totals(serial, selection, required);
#if !defined(LPL_PLUGIN_UNAVAILABLE)
    if (stage == KERNEL_TEST_STAGE_BOOTED)
        kernel_test_run_libraries(serial, selection, required);
#endif
}

uint32_t kernel_test_failure_count(void) { return kernel_test_failed_count; }
