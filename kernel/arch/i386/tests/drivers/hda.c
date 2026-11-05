#include <kernel/cpu/paging.h>
#include <kernel/drivers/hda.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(intel_high_definition_audio, KERNEL_TEST_STAGE_BOOTED);

#define INTEL_HIGH_DEFINITION_AUDIO_TEST_UNCACHED_BITS 3u /**< PCD and PWT set, PAT clear: PAT entry 3, UC. */
#define INTEL_HIGH_DEFINITION_AUDIO_TEST_PAGE_SIZE_EXTENSIONS                                                          \
    (1u << 4) /**< CR4.PSE: a page directory entry may map a 4 MiB page. */

/**
 * @brief The PAT, PCD and PWT bits of @p entry, which select its memory type.
 */
static uint32_t intel_high_definition_audio_test_memory_type_bits(const PageTableEntry_t *entry)
{
    return ((uint32_t) entry->page_attribute_table << 2u) | ((uint32_t) entry->cache_disable << 1u) |
           entry->write_through;
}

/**
 * @brief Reads the memory type bits of the driver's mapping at @p page and of the kernel's map entry for its frame.
 *
 * @return true when both entries exist and the kernel's map entry holds the same frame, false otherwise.
 */
static bool intel_high_definition_audio_test_read_both_types(uint32_t page, uint32_t *out_driver_bits,
                                                             uint32_t *out_kernel_map_bits)
{
    PageTableEntry_t driver_entry;
    PageTableEntry_t kernel_map_entry;

    if (!paging_get_page_table_entry(page, &driver_entry))
        return false;

    const uint32_t frame = (uint32_t) driver_entry.page_frame_base << 12u;

    if (!paging_get_page_table_entry(frame + KERNEL_VIRTUAL_BASE, &kernel_map_entry) ||
        ((uint32_t) kernel_map_entry.page_frame_base << 12u) != frame)
        return false;
    *out_driver_bits = intel_high_definition_audio_test_memory_type_bits(&driver_entry);
    *out_kernel_map_bits = intel_high_definition_audio_test_memory_type_bits(&kernel_map_entry);
    return true;
}

/**
 * @brief Each page the controller reads or writes has one memory type, uncached, in the driver's mapping and in the
 *        kernel's map of physical memory alike.
 *
 * @details Intel SDM vol. 3, §14.12.4: a page mapped with two memory types is undefined behaviour. The physical memory
 *          manager hands out frames the kernel's map holds write-back, each in a 4 KiB entry of its own since CR4.PSE
 *          is clear, and the driver maps them a second time for the controller. The driver sets up none of the
 *          vendor registers that make a controller snoop the processor's caches, so the type both mappings must
 *          agree on is uncached. The bring-up runs here unless an earlier test left the rings running, and a capture
 *          it started is stopped again.
 */
KERNEL_TEST(every_direct_memory_access_page_has_one_memory_type)
{
    const IntelHighDefinitionAudioState_t *const state = intel_high_definition_audio_state();
    const bool brought_up_here = !state->rings_running;

    if (brought_up_here)
        (void) intel_high_definition_audio_initialize(NULL);
    if (!state->controller_present)
    {
        kernel_test_skip(test, "no Intel HDA controller on this machine");
        return;
    }

    uint32_t one_type = 0u;
    uint32_t uncached = 0u;
    uint32_t first_driver_bits = 0u;
    uint32_t first_kernel_map_bits = 0u;

    for (uint32_t i = 0u; i < state->direct_memory_access_pages_claimed; ++i)
    {
        uint32_t driver_bits = 0u;
        uint32_t kernel_map_bits = 0u;

        if (!intel_high_definition_audio_test_read_both_types(state->direct_memory_access_pages[i], &driver_bits,
                                                              &kernel_map_bits))
            continue;
        if (i == 0u)
        {
            first_driver_bits = driver_bits;
            first_kernel_map_bits = kernel_map_bits;
        }
        if (driver_bits == kernel_map_bits)
            ++one_type;
        if (driver_bits == INTEL_HIGH_DEFINITION_AUDIO_TEST_UNCACHED_BITS &&
            kernel_map_bits == INTEL_HIGH_DEFINITION_AUDIO_TEST_UNCACHED_BITS)
            ++uncached;
    }
    if (brought_up_here)
        (void) intel_high_definition_audio_stop_capture();

    kernel_test_measure(test, "pages_claimed", state->direct_memory_access_pages_claimed);
    kernel_test_measure(test, "pages_with_one_type", one_type);
    kernel_test_measure(test, "pages_uncached", uncached);
    kernel_test_measure(test, "first_driver_type_bits", first_driver_bits);
    kernel_test_measure(test, "first_kernel_map_type_bits", first_kernel_map_bits);
    kernel_test_check(
        test, (asmutils_read_control_register_4() & INTEL_HIGH_DEFINITION_AUDIO_TEST_PAGE_SIZE_EXTENSIONS) == 0u,
        "no page directory entry maps a large page, so each frame has a kernel's map entry of its own");
    kernel_test_check(test, state->direct_memory_access_pages_claimed == (state->capture_converter != 0u ? 4u : 2u),
                      "the bring-up claimed both rings, and the capture descriptor list and buffer when it found a "
                      "capture converter");
    kernel_test_check(test, one_type == state->direct_memory_access_pages_claimed,
                      "each page has the same memory type bits in the driver's mapping and in the kernel's map");
    kernel_test_check(test, uncached == state->direct_memory_access_pages_claimed,
                      "both mappings of each page are uncached, since the driver makes no controller snoop");
}
