#include "libassistant/libassistant.h"

#include <lpl/infer/ModelImage.hpp>
#include <lpl/infer/Parity.hpp>
#include <lpl/testing/Test.hpp>

LPL_TEST_SUITE(model_slot);

namespace {

constexpr lpl::core::u32 kHeaderAlignmentWord = 12u;
constexpr lpl::core::u32 kRetiredVersion = 1u;

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
 * @brief In ring 0, a loaded slot holding the parity model's version 2 image stays loaded; its alignment word set to
 *        2^11, it becomes malformed by the name of that refusal; a version 1, no longer read, and a version 3 are
 *        malformed; an empty slot is left empty.
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
    test.check(image != nullptr && model.writeBlob(image, size) == size, "its version 2 image is written");
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

    image[4] = static_cast<lpl::core::u8>(kRetiredVersion);
    ModelSlot_t version1 = loadedSlot(image, size);
    const char *const version1Refusal = libassistant_judge_model_slot(&version1);
    image[4] = 3u;
    ModelSlot_t version3 = loadedSlot(image, size);
    const char *const version3Refusal = libassistant_judge_model_slot(&version3);
    image[4] = static_cast<lpl::core::u8>(lpl::infer::kModelImageVersion2);
    test.check(sameText(version1Refusal, "not_version_2") && version1.state == MODEL_SLOT_MALFORMED,
               "a version 1, no longer read, makes it malformed");
    test.check(sameText(version3Refusal, "not_version_2") && version3.state == MODEL_SLOT_MALFORMED,
               "a version 3 makes it malformed");

    ModelSlot_t empty{};
    test.check(libassistant_judge_model_slot(&empty) == nullptr && empty.state == MODEL_SLOT_EMPTY,
               "an empty slot is left empty");
    test.measure("image_bytes", size);
}
