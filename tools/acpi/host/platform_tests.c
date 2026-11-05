#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(host_platform, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief The build machine's stand-in for the VMM commits pages of a reservation only: a commit that runs past its
 *        last page is refused, where ring 0 cannot tell whose reservation the next page belongs to, and the pages it
 *        holds still commit.
 */
KERNEL_TEST(a_commit_past_the_end_of_a_reservation_is_refused)
{
    uint8_t *pages = kernel_vmm_reserve_pages(2u);

    if (!kernel_test_check(test, pages != NULL, "two pages are reserved"))
        return;
    kernel_test_check(test, !kernel_vmm_commit_pages(pages + 4096u, 2u),
                      "a commit of its last page and the one after it is refused");
    kernel_test_check(test, kernel_vmm_commit_pages(pages, 2u), "a commit of its two pages is not");
    pages[0] = 1u;
    pages[8191] = 2u;
    kernel_test_check(test, pages[0] == 1u && pages[8191] == 2u, "and both can be written");
    kernel_vmm_free_pages(pages, 2u);
}
