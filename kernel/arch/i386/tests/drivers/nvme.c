#include <kernel/cpu/pci.h>
#include <kernel/drivers/nvme.h>
#include <kernel/memory/pinned_memory.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <string.h>

/** The signature tools/nvme-test-image.py puts at the start of a test image. */
#define NON_VOLATILE_MEMORY_EXPRESS_TEST_SIGNATURE "LPLKERNEL NVME TEST IMAGE"

/** Bytes the signature takes at the start of the image, padded with zeros. */
#define NON_VOLATILE_MEMORY_EXPRESS_TEST_SIGNATURE_BYTES 32u

/** Bytes of the buffer the tests read into: nine pages, so a 32 KiB read can start mid-page. */
#define NON_VOLATILE_MEMORY_EXPRESS_TEST_BUFFER_BYTES (9u * 4096u)

/**
 * @brief One read the image test makes, chosen for the way its buffer is described.
 */
typedef struct NonVolatileMemoryExpressTestRead {
    uint32_t first_block;   /**< The first block read, counted in blocks of 512 bytes. */
    uint32_t bytes;         /**< Bytes read. */
    uint32_t buffer_offset; /**< Where in the buffer the read lands. */
    const char *claim;      /**< What the read checks. */
} NonVolatileMemoryExpressTestRead_t;

static const NonVolatileMemoryExpressTestRead_t non_volatile_memory_express_test_reads[] = {
    {1u,   512u,   0u,   "one block, inside one page"                            },
    {8u,   4096u,  0u,   "one page, through PRP entry 1 alone"                   },
    {16u,  8192u,  0u,   "two pages, through PRP entry 2"                        },
    {64u,  32768u, 0u,   "eight pages, through a list of PRP entries"            },
    {100u, 4096u,  512u, "one page's worth from mid-page, across a page boundary"},
};

/**
 * @brief The byte tools/nvme-test-image.py writes at @p offset of a test image.
 */
static uint8_t non_volatile_memory_express_test_byte(uint64_t offset)
{
    if (offset < NON_VOLATILE_MEMORY_EXPRESS_TEST_SIGNATURE_BYTES)
        return offset < sizeof(NON_VOLATILE_MEMORY_EXPRESS_TEST_SIGNATURE) - 1u ?
                   (uint8_t) NON_VOLATILE_MEMORY_EXPRESS_TEST_SIGNATURE[offset] :
                   0u;
    return (uint8_t) ((uint32_t) (offset >> 9) * 251u + (uint32_t) (offset & 0x1FFu) * 13u + 7u);
}

/**
 * @brief Counts the bytes of @p data that differ from the image from @p first_offset on, each
 *        expected byte first XORed with @p flip.
 */
static uint32_t non_volatile_memory_express_test_mismatches(const uint8_t *data, uint64_t first_offset, uint32_t bytes,
                                                            uint8_t flip)
{
    uint32_t mismatches = 0u;

    for (uint32_t i = 0u; i < bytes; ++i)
        if (data[i] != (uint8_t) (non_volatile_memory_express_test_byte(first_offset + i) ^ flip))
            ++mismatches;
    return mismatches;
}

/**
 * @brief Fills @p data with the image's bytes from @p first_offset on, each XORed with @p flip.
 */
static void non_volatile_memory_express_test_fill(uint8_t *data, uint64_t first_offset, uint32_t bytes, uint8_t flip)
{
    for (uint32_t i = 0u; i < bytes; ++i)
        data[i] = (uint8_t) (non_volatile_memory_express_test_byte(first_offset + i) ^ flip);
}

/**
 * @brief Brings the controller up unless an earlier test left its I/O queues ready, reads the
 *        first block of namespace 1 into @p buffer, and says whether the test may go on.
 *
 * @details A first block that does not read fails the test. Only a first block that starts with
 *          the test image's signature lets it go on, so nothing is ever written to a drive whose
 *          signature was not read.
 *
 * @return true when the drive is a test image; false when the test skipped or failed.
 */
static bool non_volatile_memory_express_test_on_an_image(KernelTest_t *test, uint8_t *buffer)
{
    const NonVolatileMemoryExpressState_t *const state = non_volatile_memory_express_state();

    if (!state->input_output_queues_ready)
        (void) non_volatile_memory_express_initialize();
    if (!state->controller_present)
    {
        kernel_test_skip(test, "no NVM Express controller on the bus");
        return false;
    }
    if (buffer == NULL)
    {
        kernel_test_skip(test, "no pinned memory for the buffer");
        return false;
    }

    const bool first_block_read = non_volatile_memory_express_read_blocks(0u, 1u, buffer);

    kernel_test_check(test, first_block_read, "the first block of namespace 1 reads");
    if (!first_block_read)
        return false;
    if (memcmp(buffer, NON_VOLATILE_MEMORY_EXPRESS_TEST_SIGNATURE,
               sizeof(NON_VOLATILE_MEMORY_EXPRESS_TEST_SIGNATURE) - 1u) != 0)
    {
        kernel_test_skip(test, "the drive is not a test image, and nothing is written to it");
        return false;
    }
    return true;
}

KERNEL_TEST_SUITE(non_volatile_memory_express, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The controller comes out of reset with its admin queues, and identifies itself and namespace 1.
 *
 * @details Skipped on a machine with no NVM Express controller. A boot that attaches one names this
 *          test on `lpl.test.require=`, so a skip there fails.
 */
KERNEL_TEST(the_controller_comes_up_and_identifies_its_namespace)
{
    const bool identified = non_volatile_memory_express_initialize();
    const NonVolatileMemoryExpressState_t *const state = non_volatile_memory_express_state();

    if (!state->controller_present)
    {
        kernel_test_skip(test, "no NVM Express controller on the bus");
        return;
    }

    kernel_test_check(test, state->controller_ready, "the controller came out of reset with its admin queues");
    kernel_test_check(test, state->identified, "it answered Identify for itself and for namespace 1");
    kernel_test_check(test, identified && state->input_output_queues_ready,
                      "it took an I/O submission queue and an I/O completion queue");
    kernel_test_check(test, state->maximum_transfer_bytes >= 4096u, "one command moves at least a page");
    kernel_test_check(test, state->version >= 0x00010000u, "its version is 1.0 or later");
    kernel_test_check(test, state->namespace_count >= 1u, "it has at least one namespace");
    kernel_test_check(test, state->namespace_blocks > 0u && state->block_bytes >= 512u,
                      "namespace 1 has blocks of at least 512 bytes");
    kernel_test_check(test, state->serial_number[0] != '\0' && state->model_number[0] != '\0',
                      "it gave a serial number and a model number");
    if (state->failure != NULL)
        kernel_test_note(test, state->failure);

    kernel_test_measure_hexadecimal(test, "version", state->version);
    kernel_test_measure(test, "maximum_queue_entries", state->maximum_queue_entries);
    kernel_test_measure(test, "doorbell_stride_bytes", state->doorbell_stride_bytes);
    kernel_test_measure(test, "ready_timeout_milliseconds", state->ready_timeout_milliseconds);
    kernel_test_measure(test, "namespace_count", state->namespace_count);
    kernel_test_measure(test, "namespace_blocks", (uint32_t) state->namespace_blocks);
    kernel_test_measure(test, "block_bytes", state->block_bytes);
    kernel_test_measure(test, "maximum_transfer_bytes", state->maximum_transfer_bytes);
}

/**
 * @brief A second bring-up stops the running controller, and starts it again with fresh admin queues.
 *
 * @details Whatever the firmware left, this bring-up finds the controller running, with queue
 *          positions the controller remembers, so it checks that the reset makes both sides start over.
 *          SeaBIOS leaves it running too, with its own admin queues: under QEMU the first bring-up
 *          already depends on the reset.
 */
KERNEL_TEST(a_second_bring_up_resets_the_running_controller)
{
    if (!non_volatile_memory_express_state()->controller_present)
    {
        kernel_test_skip(test, "no NVM Express controller on the bus");
        return;
    }

    const bool first = non_volatile_memory_express_initialize();
    const bool second = non_volatile_memory_express_initialize();

    kernel_test_check(test, first, "the first bring-up identifies the controller");
    kernel_test_check(test, second && non_volatile_memory_express_state()->identified,
                      "and so does a second one, from a running controller");
    if (non_volatile_memory_express_state()->failure != NULL)
        kernel_test_note(test, non_volatile_memory_express_state()->failure);
}

/**
 * @brief The bring-up turns bus mastering on itself, whatever the firmware left.
 *
 * @details Firmware that boots from NVM Express turns bus mastering on, so the other tests pass
 *          whether the driver does or not. This one turns it off first: without it the controller
 *          never reads its queues, and Identify times out.
 */
KERNEL_TEST(the_bring_up_turns_bus_mastering_on_itself)
{
    const PeripheralComponentInterconnectDevice_t *const device = peripheral_component_interconnect_find_by_class(
        NON_VOLATILE_MEMORY_EXPRESS_CLASS, NON_VOLATILE_MEMORY_EXPRESS_SUBCLASS);

    if (device == NULL || !non_volatile_memory_express_state()->controller_present)
    {
        kernel_test_skip(test, "no NVM Express controller on the bus");
        return;
    }

    const uint16_t command = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND);
    peripheral_component_interconnect_config_write_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND,
        (uint16_t) (command & ~PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_BUS_MASTER));

    const bool identified = non_volatile_memory_express_initialize();
    const uint16_t after = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND);

    kernel_test_check(test, (after & PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_BUS_MASTER) != 0u,
                      "bus mastering is on again after the bring-up");
    kernel_test_check(test, identified, "and the controller answered Identify through its queues");
    if (non_volatile_memory_express_state()->failure != NULL)
        kernel_test_note(test, non_volatile_memory_express_state()->failure);
}

/**
 * @brief Reads of every shape come back with the bytes the test image holds there.
 *
 * @details Each read is described to the controller differently: inside one page, one whole page,
 *          two pages, a list of pages, and a buffer that starts mid-page. The last block of the
 *          namespace is read too, and a read past the end or larger than one command moves is
 *          refused before it reaches the controller.
 */
KERNEL_TEST(the_namespace_reads_what_the_image_holds)
{
    uint8_t *const buffer = (uint8_t *) kernel_pinned_alloc(NON_VOLATILE_MEMORY_EXPRESS_TEST_BUFFER_BYTES);
    const NonVolatileMemoryExpressState_t *const state = non_volatile_memory_express_state();
    uint32_t mismatches = 0u;

    if (!non_volatile_memory_express_test_on_an_image(test, buffer))
    {
        if (buffer != NULL)
            kernel_pinned_free(buffer, NON_VOLATILE_MEMORY_EXPRESS_TEST_BUFFER_BYTES);
        return;
    }

    for (uint32_t i = 0u;
         i < sizeof(non_volatile_memory_express_test_reads) / sizeof(non_volatile_memory_express_test_reads[0]); ++i)
    {
        const NonVolatileMemoryExpressTestRead_t *const read = &non_volatile_memory_express_test_reads[i];
        const uint64_t first_block = (uint64_t) read->first_block * 512u / state->block_bytes;
        const uint32_t block_count = read->bytes < state->block_bytes ? 1u : read->bytes / state->block_bytes;
        uint8_t *const data = buffer + read->buffer_offset;
        const uint32_t bytes = block_count * state->block_bytes;
        const bool done = non_volatile_memory_express_read_blocks(first_block, block_count, data);
        const uint32_t wrong =
            done ? non_volatile_memory_express_test_mismatches(data, first_block * state->block_bytes, bytes, 0u) :
                   bytes;

        kernel_test_check(test, done && wrong == 0u, read->claim);
        mismatches += wrong;
    }

    const uint64_t last_block = state->namespace_blocks - 1u;
    const bool last_read = non_volatile_memory_express_read_blocks(last_block, 1u, buffer);

    kernel_test_check(test,
                      last_read && non_volatile_memory_express_test_mismatches(buffer, last_block * state->block_bytes,
                                                                               state->block_bytes, 0u) == 0u,
                      "the last block of the namespace");
    kernel_test_check(test, !non_volatile_memory_express_read_blocks(state->namespace_blocks, 1u, buffer),
                      "a read past the end is refused");
    kernel_test_check(
        test,
        !non_volatile_memory_express_read_blocks(0u, state->maximum_transfer_bytes / state->block_bytes + 1u, buffer),
        "a read larger than one command moves is refused");

    kernel_test_measure(test, "mismatched_bytes", mismatches);
    kernel_pinned_free(buffer, NON_VOLATILE_MEMORY_EXPRESS_TEST_BUFFER_BYTES);
}

/**
 * @brief Written blocks read back as written, the blocks around them keep their bytes, and the
 *        image is put back.
 *
 * @details The blocks get the image's bytes inverted, so a write that did not happen cannot read
 *          back right. One block on each side is read with them: a write one block longer than
 *          asked, which a count that is not 0's based gives, inverts the block after. The image's
 *          bytes are written back last, so the next boot finds a test image again.
 */
KERNEL_TEST(written_blocks_read_back_and_their_neighbours_keep_theirs)
{
    uint8_t *const buffer = (uint8_t *) kernel_pinned_alloc(NON_VOLATILE_MEMORY_EXPRESS_TEST_BUFFER_BYTES);
    const NonVolatileMemoryExpressState_t *const state = non_volatile_memory_express_state();

    if (!non_volatile_memory_express_test_on_an_image(test, buffer))
    {
        if (buffer != NULL)
            kernel_pinned_free(buffer, NON_VOLATILE_MEMORY_EXPRESS_TEST_BUFFER_BYTES);
        return;
    }

    const uint32_t block_bytes = state->block_bytes;
    const uint32_t block_count = 8192u / block_bytes < 1u ? 1u : 8192u / block_bytes;
    const uint64_t first_block = state->namespace_blocks - block_count - 2u;
    const uint32_t bytes = block_count * block_bytes;
    const uint64_t first_offset = first_block * block_bytes;

    non_volatile_memory_express_test_fill(buffer, first_offset, bytes, 0xFFu);
    kernel_test_check(test, non_volatile_memory_express_write_blocks(first_block, block_count, buffer),
                      "the inverted blocks are written");

    memset(buffer, 0, NON_VOLATILE_MEMORY_EXPRESS_TEST_BUFFER_BYTES);
    const bool read_back = non_volatile_memory_express_read_blocks(first_block - 1u, block_count + 2u, buffer);
    const uint32_t wrong_written =
        read_back ? non_volatile_memory_express_test_mismatches(buffer + block_bytes, first_offset, bytes, 0xFFu) :
                    bytes;
    const uint32_t wrong_neighbours =
        read_back ? non_volatile_memory_express_test_mismatches(buffer, first_offset - block_bytes, block_bytes, 0u) +
                        non_volatile_memory_express_test_mismatches(buffer + block_bytes + bytes, first_offset + bytes,
                                                                    block_bytes, 0u) :
                    block_bytes;

    kernel_test_check(test, read_back && wrong_written == 0u, "they read back inverted");
    kernel_test_check(test, read_back && wrong_neighbours == 0u, "and the block on each side keeps the image's bytes");

    non_volatile_memory_express_test_fill(buffer, first_offset, bytes, 0u);
    const bool restored = non_volatile_memory_express_write_blocks(first_block, block_count, buffer) &&
                          non_volatile_memory_express_read_blocks(first_block, block_count, buffer) &&
                          non_volatile_memory_express_test_mismatches(buffer, first_offset, bytes, 0u) == 0u;

    kernel_test_check(test, restored, "the image's bytes are written back");
    kernel_test_measure(test, "blocks_written", block_count * 2u);
    kernel_test_measure(test, "mismatched_bytes", wrong_written + wrong_neighbours);
    kernel_pinned_free(buffer, NON_VOLATILE_MEMORY_EXPRESS_TEST_BUFFER_BYTES);
}
