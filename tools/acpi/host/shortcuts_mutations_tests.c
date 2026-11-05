#include "shortcuts.h"

#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(host_shortcuts, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief Every byte of a table of data changed to its value with one bit flipped, to 0x00 and to 0xFF, each change
 *        loading to the same namespace, faults and counts of what it declared with the load's shortcuts and without,
 *        and to the same fold once every object kept in the table is built: the shortcuts hold for each byte changed
 *        to each of three values, whether the decoder builds it as an integer, the load keeps it in its table, or
 *        neither. Ring 0 loads a sample of these changes.
 */
KERNEL_TEST(every_one_byte_change_of_a_table_of_data_declares_the_same_both_ways)
{
    ShortcutsMutations_t mutations;

    shortcuts_change_bytes(test, SHORTCUTS_TABLE_DATA, 1u, true, &mutations);
    kernel_test_check(test,
                      mutations.loads == 3u * mutations.bytes && mutations.taken.data_stepped_over != 0u &&
                          mutations.data_kept != 0u && mutations.taken.integers_built_directly != 0u,
                      "every change was loaded both ways, and the shortcuts stepped over data, kept some and built "
                      "integers");
    kernel_test_check(test, mutations.disagreements == 0u,
                      "every change declares the same with the shortcut and without");
    kernel_test_measure(test, "mutants", mutations.loads);
    kernel_test_measure(test, "data_stepped_over", mutations.taken.data_stepped_over);
    kernel_test_measure(test, "data_kept", mutations.data_kept);
    kernel_test_measure(test, "integers_built_directly", mutations.taken.integers_built_directly);
}

/**
 * @brief Every byte of the statements the decoder takes changed to its value with one bit flipped, to 0x00 and to
 *        0xFF, each change loading to the same namespace, faults and counts of what it declared with the load's
 *        shortcuts and without: the decoder holds for each byte changed to each of three values, whether it takes the
 *        statement or hands it back. Ring 0 loads a sample of these changes.
 */
KERNEL_TEST(every_one_byte_change_of_a_table_of_statements_declares_the_same_both_ways)
{
    ShortcutsMutations_t mutations;

    shortcuts_change_bytes(test, SHORTCUTS_TABLE_STATEMENTS, 1u, true, &mutations);
    kernel_test_check(test,
                      mutations.loads == 3u * mutations.bytes && mutations.taken.declarations_decoded_directly != 0u,
                      "every change was loaded both ways, and the decoder took statements");
    kernel_test_check(test, mutations.disagreements == 0u,
                      "every change declares the same with the decoder and without");
    kernel_test_measure(test, "mutants", mutations.loads);
    kernel_test_measure(test, "declarations_decoded_directly", mutations.taken.declarations_decoded_directly);
}

/**
 * @brief Every byte of the OperationRegions changed to its value with one bit flipped, to 0x00 and to 0xFF, each change
 *        loading to the same namespace, faults and counts of what it declared with the load's shortcuts and without:
 *        the decoder holds for each byte changed to each of three values, whether it takes the region, hands it back at
 *        its RegionSpace or leaves it to the walk.
 */
KERNEL_TEST(every_one_byte_change_of_a_table_of_regions_declares_the_same_both_ways)
{
    ShortcutsMutations_t mutations;

    shortcuts_change_bytes(test, SHORTCUTS_TABLE_REGIONS, 1u, true, &mutations);
    kernel_test_check(test,
                      mutations.loads == 3u * mutations.bytes && mutations.taken.declarations_decoded_directly != 0u,
                      "every change was loaded both ways, and the decoder took statements");
    kernel_test_check(test, mutations.disagreements == 0u,
                      "every change declares the same with the decoder and without");
    kernel_test_measure(test, "mutants", mutations.loads);
    kernel_test_measure(test, "declarations_decoded_directly", mutations.taken.declarations_decoded_directly);
}
