#include "libassistant/libassistant.h"

#include <lpl/infer/ModelImage.hpp>
#include <lpl/infer/Parity.hpp>
#include <lpl/testing/Test.hpp>

LPL_TEST_SUITE(model_slot);

namespace {

constexpr lpl::core::u32 kHeaderAlignmentWord = 12u;
constexpr lpl::core::u32 kHeaderVersionByte = 4u;
constexpr lpl::core::u32 kImageVersion = 1u;
constexpr lpl::core::u32 kPastTheMagicBytes = 10u; ///< A module that holds the magic and no whole header.

[[nodiscard]] bool sameText(const char *lhs, const char *rhs)
{
    if (lhs == nullptr || rhs == nullptr)
        return lhs == rhs;
    while (*lhs != '\0' && *lhs == *rhs)
    {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/**
 * @brief A slot the probe found loaded with @p size bytes at @p bytes.
 */
[[nodiscard]] ModelSlot_t loadedSlot(const lpl::core::u8 *bytes, lpl::core::u32 size)
{
    ModelSlot_t slot{};

    slot.state = MODEL_SLOT_LOADED;
    slot.bytes = bytes;
    slot.size = size;
    slot.magic = lpl::infer::kModelMagic;
    return slot;
}

} // namespace

/**
 * @brief In ring 0, a loaded slot holding the parity model's image, version 1, stays loaded; its alignment word set to
 *        2^11, it becomes malformed by the name of that refusal; a version 2, as LplAssistant 0.4.0 and 0.5.0 wrote,
 *        and a version 3 are malformed as unknown versions; an empty slot is left empty.
 */
LPL_TEST(the_slot_judges_its_image_by_the_reader_s_check)
{
    lpl::infer::TensorArena arena{lpl::infer::parityArenaBytes()};
    lpl::infer::Vocab vocab;
    lpl::infer::Model model;

    test.check(lpl::infer::buildParityVocab(arena, vocab), "the canonical vocabulary builds");
    lpl::infer::ModelConfig shape = lpl::infer::parityModelConfig();
    shape.vocabSize = vocab.size();
    test.check(model.synthesise(arena, shape, vocab, lpl::infer::parityModelSeed()), "and the parity model");
    const lpl::core::u32 size = model.blobBytes();
    lpl::core::u8 *const image = size != 0u ? arena.claim<lpl::core::u8>(size) : nullptr;
    test.check(image != nullptr && model.writeBlob(image, size) == size &&
                   lpl::infer::modelImageVersion(image, size) == kImageVersion,
               "its image is written, version 1");
    if (image == nullptr)
        return;

    ModelSlot_t accepted = loadedSlot(image, size);
    const char *const acceptedRefusal = libassistant_judge_model_slot(&accepted);
    test.check(acceptedRefusal == nullptr && accepted.state == MODEL_SLOT_LOADED, "the image keeps the slot loaded");

    image[kHeaderAlignmentWord] = 11u;
    ModelSlot_t misaligned = loadedSlot(image, size);
    const char *const misalignedRefusal = libassistant_judge_model_slot(&misaligned);
    image[kHeaderAlignmentWord] = static_cast<lpl::core::u8>(lpl::infer::kModelImageDefaultAlignmentLog2);
    test.check(sameText(misalignedRefusal, "bad_alignment") && misaligned.state == MODEL_SLOT_MALFORMED,
               "an alignment of 2^11 makes it malformed, by that name");

    image[kHeaderVersionByte] = 2u;
    ModelSlot_t version2 = loadedSlot(image, size);
    const char *const version2Refusal = libassistant_judge_model_slot(&version2);
    image[kHeaderVersionByte] = 3u;
    ModelSlot_t version3 = loadedSlot(image, size);
    const char *const version3Refusal = libassistant_judge_model_slot(&version3);
    image[kHeaderVersionByte] = static_cast<lpl::core::u8>(kImageVersion);
    test.check(sameText(version2Refusal, "unknown_version") && version2.state == MODEL_SLOT_MALFORMED,
               "a version 2, as LplAssistant 0.4.0 and 0.5.0 wrote, makes it malformed as an unknown version");
    test.check(sameText(version3Refusal, "unknown_version") && version3.state == MODEL_SLOT_MALFORMED,
               "and so does a version 3");

    ModelSlot_t truncated = loadedSlot(image, kPastTheMagicBytes);
    const char *const truncatedRefusal = libassistant_judge_model_slot(&truncated);
    test.check(sameText(truncatedRefusal, "too_short") && truncated.state == MODEL_SLOT_MALFORMED,
               "10 bytes past the magic make it malformed as too short, by the reader that knows the header");

    ModelSlot_t empty{};
    test.check(libassistant_judge_model_slot(&empty) == nullptr && empty.state == MODEL_SLOT_EMPTY,
               "an empty slot is left empty");
    test.measure("image_bytes", size);
}
