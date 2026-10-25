#include "simulated_host.h"

#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_corpus, KERNEL_TEST_STAGE_INITIALIZATION);

#define CORPUS_IMAGE_CAPACITY 256u        /**< Bytes of the buffer a made-up image is written in. */
#define CORPUS_HEADER_BYTES   24u         /**< Bytes of an image's header: magic, root pointer, range count. */
#define CORPUS_RANGE_BYTES    16u         /**< Bytes of a range's head: physical address, length. */
#define CORPUS_ROOT_ADDRESS   0x000E0010u /**< The root pointer of the made-up images. */
#define CORPUS_LOW_ADDRESS    0x000E0000u /**< Where the first range of a made-up image lies. */
#define CORPUS_HIGH_ADDRESS   0x7FFE0000u /**< Where the second range of a made-up image lies. */
#define CORPUS_LAST_ADDRESS   0xFFFFFFFFFFFFFFF0ull /**< A range there wraps past the last address. */
#define CORPUS_TOO_MANY_RANGES                                                                                         \
    (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_RANGES + 1u) /**< One more than a host holds. */

static uint8_t corpus_image[CORPUS_IMAGE_CAPACITY];
static uint32_t corpus_image_length;
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t corpus_host;

/**
 * @brief Appends @p value to the made-up image, as @p width little-endian bytes, written one by one
 *        so that the layout tested is the documented one and not a structure's.
 */
static void corpus_image_put(uint64_t value, uint32_t width)
{
    for (uint32_t byte = 0u; byte < width; ++byte)
        corpus_image[corpus_image_length++] = (uint8_t) (value >> (8u * byte));
}

/**
 * @brief Starts a made-up image: @p magic, then the root pointer and the range count.
 */
static void corpus_image_header(const char *magic, uint64_t root, uint64_t range_count)
{
    memset(corpus_image, 0, sizeof(corpus_image));
    corpus_image_length = 0u;
    for (uint32_t letter = 0u; letter < 8u; ++letter)
        corpus_image_put((uint8_t) magic[letter], 1u);
    corpus_image_put(root, 8u);
    corpus_image_put(range_count, 8u);
}

/**
 * @brief Appends a range to the made-up image: its head, its @p length bytes, then zeros to a
 *        multiple of eight.
 */
static void corpus_image_range(uint64_t physical_address, const char *bytes, uint32_t length)
{
    corpus_image_put(physical_address, 8u);
    corpus_image_put(length, 8u);
    memcpy(corpus_image + corpus_image_length, bytes, length);
    corpus_image_length += (length + 7u) & ~7u;
}

/**
 * @brief Whether the @p length bytes the host maps at @p physical_address are @p expected, unmapping
 *        them after.
 */
static bool corpus_serves(uint64_t physical_address, const char *expected, uint32_t length)
{
    const void *mapped = corpus_host.host.map_firmware(corpus_host.host.context, physical_address, length);
    const bool same = (mapped != NULL && memcmp(mapped, expected, length) == 0);

    if (mapped != NULL)
        corpus_host.host.unmap_firmware(corpus_host.host.context, mapped, length);
    return same;
}

/**
 * @brief Checks that the made-up image is refused for @p reason, and that the host serves nothing.
 */
static void corpus_refuses(KernelTest_t *test, const char *reason, const char *claim)
{
    const char *error = NULL;
    const bool opened = advanced_configuration_and_power_interface_simulated_host_open_image(
        &corpus_host, corpus_image, corpus_image_length, &error);

    kernel_test_check(test, !opened && error != NULL && memcmp(error, reason, strlen(reason) + 1u) == 0, claim);
    kernel_test_check(test, corpus_host.range_count == 0u && corpus_host.root_system_description_pointer == 0u,
                      "a refused image serves nothing");
}

/**
 * @brief An image serves each range at its address and its root pointer, its bytes in place, and
 *        neither its padding nor a byte past a range.
 */
KERNEL_TEST(an_image_serves_each_range_where_it_says)
{
    const char *error = NULL;

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 2u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_image_range(CORPUS_HIGH_ADDRESS, "DSDT", 4u);

    const bool opened = advanced_configuration_and_power_interface_simulated_host_open_image(
        &corpus_host, corpus_image, corpus_image_length, &error);

    if (!kernel_test_check(test, opened, "a whole image is opened"))
    {
        kernel_test_note(test, error);
        return;
    }
    kernel_test_check(test, corpus_host.root_system_description_pointer == CORPUS_ROOT_ADDRESS,
                      "the root pointer is the image's");
    kernel_test_check(test, corpus_host.range_count == 2u, "each range of the image is placed");
    kernel_test_check(test, corpus_serves(CORPUS_LOW_ADDRESS + 4u, "PTR AND", 7u),
                      "the first range is served at its address");
    kernel_test_check(test, corpus_serves(CORPUS_HIGH_ADDRESS, "DSDT", 4u),
                      "the second range is served at its address");
    const void *second = corpus_host.host.map_firmware(corpus_host.host.context, CORPUS_HIGH_ADDRESS, 1u);

    kernel_test_check(test, second == corpus_image + CORPUS_HEADER_BYTES + 2u * CORPUS_RANGE_BYTES + 24u,
                      "a range's bytes are served in place, after the first range padded to 24 bytes");
    if (second != NULL)
        corpus_host.host.unmap_firmware(corpus_host.host.context, second, 1u);
    kernel_test_check(test, !corpus_serves(CORPUS_HIGH_ADDRESS, "DSDT\0", 5u), "no byte past a range is served");
    kernel_test_check(test, !corpus_serves(CORPUS_LOW_ADDRESS + 20u, "\0", 1u), "a range's padding is not served");
    kernel_test_check(test, corpus_host.outstanding_maps == 0u && corpus_host.mismatched_unmaps == 0u,
                      "every map is unmapped as it was mapped");
}

/**
 * @brief An image cut short, misnamed, holding too many ranges, an empty range, a range that wraps or
 *        overlaps another, or bytes after its last range is refused, with what it does wrong.
 */
KERNEL_TEST(a_malformed_image_is_refused_and_says_why)
{
    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 0u);
    --corpus_image_length;
    corpus_refuses(test, "the image is shorter than its header", "an image cut inside its header is refused");

    corpus_image_header("LPLACPIX", CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR ", 8u);
    corpus_refuses(
        test, "the image does not start with " ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC,
        "an image of another magic is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS,
                        CORPUS_TOO_MANY_RANGES);
    corpus_refuses(test, "the image holds more ranges than a simulated host",
                   "an image of more ranges than a host holds is refused before any is read");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR ", 8u);
    corpus_image_length -= 8u + 4u;
    corpus_refuses(test, "a range runs past the end of the image", "an image cut inside a range's head is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_image_length -= 4u;
    corpus_refuses(test, "a range runs past the end of the image", "an image cut inside a range's padding is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "", 0u);
    corpus_refuses(test, "a range is empty", "an empty range is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LAST_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_refuses(test, "a range wraps past the last address", "a range that wraps is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 2u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_image_range(CORPUS_LOW_ADDRESS + 19u, "DSDT", 4u);
    corpus_refuses(test, "a range overlaps another", "a range that overlaps another by one byte is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR ", 8u);
    corpus_image_length += 8u;
    corpus_refuses(test, "the image holds bytes after its last range", "an image longer than its ranges is refused");
}
