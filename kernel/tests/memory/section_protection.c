#include <kernel/memory/section_protection.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(section_protection, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief Probe targets on either side of `_kernel_read_only_end`: a constant in .rodata, and a byte
 *        in .data that must stay writable. A probe that faulted on everything would pass the first
 *        and fail the second.
 */
static const uint8_t section_protection_test_constant = 0x5Au;
static uint8_t section_protection_test_scratch = 0xA5u;

/**
 * @brief A function whose address the probe writes to, so the .text probe aims at code of its own.
 */
static void section_protection_test_code(void) {}

/**
 * @brief A write into .rodata and into .text faults, and the same write into .data does not.
 *
 * @details CR0.WP is checked apart from the protection itself: with it clear, the page bits read
 *          the same and stop nothing in ring 0.
 */
KERNEL_TEST(writes_fault_only_where_read_only)
{
    const uint8_t scratch_before = section_protection_test_scratch;

    kernel_test_check(test, kernel_section_protection_write_protect_is_enabled(),
                      "CR0.WP makes ring 0 honour read-only pages");
    kernel_test_check(test, kernel_section_protection_is_active(), "the read-only sections are protected");
    kernel_test_check(
        test, kernel_section_protection_probe_write((volatile uint8_t *) (uintptr_t) &section_protection_test_constant),
        "a write into .rodata faults");
    kernel_test_check(
        test, kernel_section_protection_probe_write((volatile uint8_t *) (uintptr_t) &section_protection_test_code),
        "a write into .text faults");
    kernel_test_check(test, !kernel_section_protection_probe_write(&section_protection_test_scratch),
                      "a write into .data still succeeds");
    kernel_test_check(test, section_protection_test_scratch == scratch_before, "the probe leaves .data unchanged");
    kernel_test_check(test, section_protection_test_constant == 0x5Au, "the constant is unchanged");
    kernel_test_measure(test, "recovered_faults", kernel_section_protection_get_recovered_fault_count());
    kernel_test_measure(test, "read_only_pages", kernel_section_protection_get_read_only_page_count());
}
