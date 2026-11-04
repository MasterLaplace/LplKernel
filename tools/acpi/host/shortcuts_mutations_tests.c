#include "shortcuts.h"

#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(host_shortcuts, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief Every byte of a table of data changed to its value with one bit flipped, to 0x00 and to 0xFF, each change
 *        loading to the same namespace, faults and counts of what it declared with the load's shortcuts and without:
 *        the shortcut holds wherever the decoder accepts or refuses the bytes. Ring 0 loads a sample of these
 *        changes.
 */
KERNEL_TEST(every_one_byte_change_of_a_table_of_data_declares_the_same_both_ways)
{
    ShortcutsMutations_t mutations;

    shortcuts_change_bytes(test, SHORTCUTS_TABLE_DATA, 1u, true, &mutations);
    kernel_test_check(test, mutations.loads == 3u * mutations.bytes && mutations.taken.data_stepped_over != 0u,
                      "every change was loaded both ways, and the shortcut stepped over data");
    kernel_test_check(test, mutations.disagreements == 0u,
                      "every change declares the same with the shortcut and without");
    kernel_test_measure(test, "mutants", mutations.loads);
    kernel_test_measure(test, "data_stepped_over", mutations.taken.data_stepped_over);
}
