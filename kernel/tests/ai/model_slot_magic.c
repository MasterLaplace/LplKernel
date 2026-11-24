#include <kernel/ai/model_slot.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(model_slot_magic, KERNEL_TEST_STAGE_BOOTED);

/** A module's length: past the magic, short of any header. */
#define MODEL_SLOT_MAGIC_TEST_BYTES 10u

/** The first word of a GGUF file, which the slot does not read: "GGUF" little-endian. */
#define MODEL_SLOT_MAGIC_TEST_GGUF 0x46554747u

/**
 * @brief A module is judged by its magic alone: 10 bytes that open on "LPLM" are loaded, the header's size left to the
 *        reader that knows it; 3 bytes, too few for the magic, or 10 that open on "GGUF", are malformed.
 */
KERNEL_TEST(a_module_is_judged_by_its_magic_alone)
{
    const uint8_t lplm[MODEL_SLOT_MAGIC_TEST_BYTES] = {'L', 'P', 'L', 'M', 1u, 0u, 0u, 0u, 76u, 0u};
    const uint8_t gguf[MODEL_SLOT_MAGIC_TEST_BYTES] = {'G', 'G', 'U', 'F', 3u, 0u, 0u, 0u, 0u, 0u};
    ModelSlot_t slot = {0};

    kernel_test_check(test,
                      kernel_model_slot_judge_magic(lplm, MODEL_SLOT_MAGIC_TEST_BYTES, &slot) &&
                          slot.state == MODEL_SLOT_LOADED && slot.magic == KERNEL_MODEL_SLOT_MAGIC &&
                          slot.bytes == lplm && slot.size == MODEL_SLOT_MAGIC_TEST_BYTES,
                      "10 bytes that open on LPLM are loaded");
    kernel_test_check(
        test, !kernel_model_slot_judge_magic(lplm, 3u, &slot) && slot.state == MODEL_SLOT_MALFORMED && slot.magic == 0u,
        "3 bytes cannot hold the magic");
    kernel_test_check(test,
                      !kernel_model_slot_judge_magic(gguf, MODEL_SLOT_MAGIC_TEST_BYTES, &slot) &&
                          slot.state == MODEL_SLOT_MALFORMED && slot.magic == MODEL_SLOT_MAGIC_TEST_GGUF,
                      "10 bytes that open on GGUF are malformed, their first word kept");
}
