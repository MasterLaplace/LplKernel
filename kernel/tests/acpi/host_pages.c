#include "host_pages.h"

#include <stddef.h>
#include <stdint.h>

#define HOST_PAGES_BYTES 4096u /**< Bytes of one page. */

/**
 * @brief Writes a pattern at the first and last byte of page @p page of @p pages, then reads it back.
 *
 * @return Whether both bytes hold what was written.
 */
static bool host_pages_page_holds(uint8_t *pages, uint32_t page)
{
    volatile uint8_t *first = pages + (size_t) page * HOST_PAGES_BYTES;
    volatile uint8_t *last = first + HOST_PAGES_BYTES - 1u;

    *first = (uint8_t) (0xA5u + page);
    *last = (uint8_t) (0x5Au + page);
    return *first == (uint8_t) (0xA5u + page) && *last == (uint8_t) (0x5Au + page);
}

bool host_pages_keep_their_contract(KernelTest_t *test, const AdvancedConfigurationAndPowerInterfaceHost_t *host)
{
    uint8_t *pages = host->reserve_pages(host->context, HOST_PAGES_RESERVED);

    if (!kernel_test_check(test, pages != NULL && ((uintptr_t) pages % HOST_PAGES_BYTES) == 0u,
                           "four pages are reserved, page-aligned"))
        return false;

    const bool first_two = host->commit_pages(host->context, pages, 2u);
    const bool fourth = host->commit_pages(host->context, pages + 3u * HOST_PAGES_BYTES, 1u);
    const bool written = first_two && fourth && host_pages_page_holds(pages, 0u) && host_pages_page_holds(pages, 1u) &&
                         host_pages_page_holds(pages, 3u);
    const bool third = host->commit_pages(host->context, pages + 2u * HOST_PAGES_BYTES, 1u);
    const bool kept = third && host_pages_page_holds(pages, 2u) && pages[0] == 0xA5u &&
                      pages[2u * HOST_PAGES_BYTES - 1u] == 0x5Bu && pages[3u * HOST_PAGES_BYTES] == 0xA8u;

    kernel_test_check(test, written, "pages committed at the start and past a gap can be written and read back");
    kernel_test_check(test, kept, "the page in the gap commits after them, and they keep what they held");
    host->release_reservation(host->context, pages, HOST_PAGES_RESERVED, HOST_PAGES_RESERVED);

    const bool refused = !host->commit_pages(host->context, pages, 1u);

    kernel_test_check(test, refused, "a commit in a reservation given back is refused");
    return written && kept && refused;
}
