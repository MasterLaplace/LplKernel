#include "libassistant/libassistant.h"

#include <kernel/ai/tensor_arena.h>
#include <kernel/memory/section_protection.h>

#include <lpl/infer/ModelImage.hpp>
#include <lpl/infer/Parity.hpp>
#include <lpl/testing/Test.hpp>

LPL_TEST_SUITE(dialogue);

namespace {

constexpr lpl::core::u32 kPageBytes = 4096u;
constexpr lpl::core::u32 kModuleRoomPages = 21u;
constexpr lpl::core::u32 kOffWordBoundary = 2u;
constexpr lpl::core::u32 kSecondExpertEntry = 8u; ///< One-based entry of the parity image's second expert.
constexpr lpl::core::u32 kInsideTheExpert = 1000u;
constexpr lpl::core::u32 kHeaderAlignmentWord = 12u;
constexpr lpl::core::u8 kTooSmallAlignmentLog2 = 11u;

/**
 * @brief Room for the parity model's image on a page boundary, which the in-place boot turns read-only for good.
 */
alignas(kPageBytes) lpl::core::u8 gSealedModule[kModuleRoomPages * kPageBytes];

/**
 * @brief Room for the images the mind copies or refuses, which stay writable.
 */
alignas(kPageBytes) lpl::core::u8 gWritableModule[kModuleRoomPages * kPageBytes];

[[nodiscard]] bool sameText(const char *lhs, const char *rhs)
{
    while (*lhs != '\0' && *lhs == *rhs)
    {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/**
 * @brief Writes the parity model's image at @p at, which holds @p room bytes.
 * @return The image's bytes; 0 when the model did not build or the room is short.
 */
[[nodiscard]] lpl::core::u32 writeParityImage(lpl::core::u8 *at, lpl::core::u32 room)
{
    lpl::infer::TensorArena arena{lpl::infer::parityArenaBytes()};
    lpl::infer::Vocab vocab;
    lpl::infer::Model model;

    if (!lpl::infer::buildParityVocab(arena, vocab))
        return 0u;
    lpl::infer::ModelConfig shape = lpl::infer::parityModelConfig();
    shape.vocabSize = vocab.size();
    if (!model.synthesise(arena, shape, vocab, lpl::infer::parityModelSeed()))
        return 0u;
    return model.writeBlob(at, room);
}

/**
 * @brief A slot the probe would have found loaded with @p size bytes at @p bytes.
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

/**
 * @brief Puts the question through the booted mind, drops it, and checks the answer is the canonical constrained
 *        run's bytes.
 */
void checkTheCanonicalAnswer(lpl::testing::Test &test)
{
    libassistant_dialogue_result_t dialogue{};
    lpl::infer::MindFoldResult canonical{};

    test.check(libassistant_dialogue_round_trip(&dialogue), "the sovereign reads back a whole, valid call");
    libassistant_shutdown();
    lpl::infer::foldMindState(canonical, kernel_tensor_arena_base(), kernel_tensor_arena_size());
    test.check(dialogue.answer_sig == canonical.textSignature,
               "the answer is the bytes the canonical constrained run decodes to");
}

} // namespace

/**
 * @brief The mind boots in the kernel's tensor arena, and a question put through the dialogue
 *        aperture comes back whole: nothing dropped, the reply inside its token budget, and its
 *        bytes the ones the canonical constrained run of gate P14 mind decodes to.
 *
 * @details Ring 0 alone: the aperture, the tensor arena and the budget are the kernel's. The
 *          canonical run is folded afterwards in the same arena, once the live mind is dropped,
 *          because the fold carves the arena again from its base.
 */
LPL_TEST(a_question_crosses_the_aperture_and_back)
{
    const bool booted = libassistant_boot(libassistant_recommended_arena_bytes());

    test.check(booted, "the mind boots in the kernel's tensor arena");
    test.check(!sameText(libassistant_model_slot_state(), "malformed"), "a weights module, if one was booted, parsed");
    if (!booted)
        return;

    libassistant_dialogue_result_t dialogue{};

    test.check(libassistant_dialogue_round_trip(&dialogue), "the sovereign reads back a whole, valid call");
    test.check(dialogue.dropped == 0u, "the aperture drops nothing");
    test.check(dialogue.budget_denied == 0u, "the reply fits its token budget");
    test.check(dialogue.delivered == dialogue.answer_bytes, "every byte the demon offered was read back");

    lpl::infer::MindFoldResult canonical{};

    libassistant_shutdown();
    lpl::infer::foldMindState(canonical, kernel_tensor_arena_base(), kernel_tensor_arena_size());
    kernel_tensor_arena_record_used(canonical.arenaBytes);
    test.check(dialogue.answer_sig == canonical.textSignature,
               "the answer is the bytes the canonical constrained run decodes to");

    test.measure("question_bytes", dialogue.question_bytes);
    test.measure("consumed", dialogue.consumed);
    test.measure("answer_bytes", dialogue.answer_bytes);
    test.measure("budget_spent", dialogue.budget_spent);
    test.measureHexadecimal("answer_signature", dialogue.answer_sig);
}

/**
 * @brief Booted from a slot holding the parity model's image on a page boundary, the mind reads the weights where they
 *        lie, turns every whole page of the image read-only, a write there faulting and the page it only touches
 *        staying writable, and answers with the canonical bytes.
 */
LPL_TEST(the_mind_reads_a_module_on_a_page_boundary_where_it_lies_and_seals_its_pages)
{
    const lpl::core::u32 size = writeParityImage(gSealedModule, sizeof gSealedModule);
    const ModelSlot_t slot = loadedSlot(gSealedModule, size);

    test.check(size != 0u, "the parity model's image is written on a page boundary");
    test.check(libassistant_boot_from_slot(0u, &slot), "the mind boots from it");
    test.check(sameText(libassistant_model_origin(), "module_in_place") &&
                   sameText(libassistant_model_reading(), "read") &&
                   sameText(libassistant_model_slot_state(), "loaded"),
               "its weights read where they lie");

    const lpl::core::u32 wholePages = size / kPageBytes;
    bool everyWholePageFaults = wholePages != 0u;
    for (lpl::core::u32 page = 0u; everyWholePageFaults && page < wholePages; ++page)
        everyWholePageFaults = kernel_section_protection_probe_write(gSealedModule + page * kPageBytes);

    test.check(libassistant_model_protected_pages() == wholePages, "each whole page of the image turned read-only");
    test.check(libassistant_model_index_fallbacks() == 0u, "its vocabulary indexed without a fallback");
    test.check(everyWholePageFaults, "a write into any of them faults");
    test.check(!kernel_section_protection_probe_write(gSealedModule + wholePages * kPageBytes),
               "and the page the image only touches stays writable");
    checkTheCanonicalAnswer(test);
    test.measure("image_bytes", size);
    test.measure("protected_pages", libassistant_model_protected_pages());
}

/**
 * @brief Booted from a slot holding the image two bytes past a word boundary, the mind copies the weights, seals
 *        nothing, and answers with the canonical bytes.
 */
LPL_TEST(the_mind_copies_a_module_off_a_word_boundary)
{
    lpl::core::u8 *const image = gWritableModule + kOffWordBoundary;
    const lpl::core::u32 size = writeParityImage(image, sizeof gWritableModule - kOffWordBoundary);
    const ModelSlot_t slot = loadedSlot(image, size);

    kernel_tensor_arena_release();
    test.check(size != 0u, "the parity model's image is written two bytes past a word boundary");
    test.check(libassistant_boot_from_slot(0u, &slot), "the mind boots from it");
    test.check(sameText(libassistant_model_origin(), "module_copied") &&
                   sameText(libassistant_model_reading(), "misaligned"),
               "its weights copied, the image misaligned");
    test.check(libassistant_model_protected_pages() == 0u &&
                   !kernel_section_protection_probe_write(gWritableModule + kPageBytes),
               "and no page of it sealed");
    test.check(kernel_tensor_arena_size() == libassistant_recommended_arena_bytes() + size,
               "in an arena grown by the image's bytes");
    checkTheCanonicalAnswer(test);
}

/**
 * @brief A slot whose image fails a signature, or an empty one, leaves the mind on its seed, the first slot malformed
 *        by the reading's name, the second empty, and the mind answers with the canonical bytes.
 */
LPL_TEST(the_mind_takes_its_seed_from_a_refused_or_an_empty_slot)
{
    const lpl::core::u32 size = writeParityImage(gWritableModule, sizeof gWritableModule);
    const ModelSlot_t refused = loadedSlot(gWritableModule, size);
    const ModelSlot_t empty{};

    test.check(size != 0u, "the parity model's image is written");
    if (size == 0u)
        return;

    const lpl::infer::ModelSegment secondExpert =
        lpl::infer::readModelSegmentEntry(gWritableModule, kSecondExpertEntry - 1u);
    test.check(secondExpert.role == lpl::infer::ModelSegmentRole::FeedForward && secondExpert.layer == 2u &&
                   secondExpert.length > kInsideTheExpert,
               "the entry changed is the second layer's expert");
    const lpl::core::u64 inExpert = secondExpert.offset + kInsideTheExpert;
    gWritableModule[inExpert] = static_cast<lpl::core::u8>(gWritableModule[inExpert] ^ 0x01u);
    test.check(libassistant_boot_from_slot(0u, &refused), "the mind boots from a slot whose second expert was changed");
    test.check(sameText(libassistant_model_origin(), "seed") &&
                   sameText(libassistant_model_reading(), "signature_mismatch") &&
                   sameText(libassistant_model_slot_state(), "malformed"),
               "on its seed, the slot malformed by the failed signature");
    checkTheCanonicalAnswer(test);
    gWritableModule[inExpert] = static_cast<lpl::core::u8>(gWritableModule[inExpert] ^ 0x01u);

    gWritableModule[kHeaderAlignmentWord] = kTooSmallAlignmentLog2;
    const ModelSlot_t misalignedTable = loadedSlot(gWritableModule, size);
    test.check(libassistant_boot_from_slot(0u, &misalignedTable), "the mind boots from a slot whose table is refused");
    gWritableModule[kHeaderAlignmentWord] = static_cast<lpl::core::u8>(lpl::infer::kModelImageDefaultAlignmentLog2);
    test.check(sameText(libassistant_model_origin(), "seed") &&
                   sameText(libassistant_model_reading(), "image_refused") &&
                   sameText(libassistant_model_slot_state(), "malformed"),
               "on its seed, the slot malformed by the table's refusal");
    checkTheCanonicalAnswer(test);

    test.check(libassistant_boot_from_slot(0u, &empty), "the mind boots from an empty slot");
    test.check(sameText(libassistant_model_origin(), "seed") && sameText(libassistant_model_reading(), "none") &&
                   sameText(libassistant_model_slot_state(), "empty"),
               "on its seed, the slot empty");
    test.check(libassistant_model_index_fallbacks() == 0u, "its seed's vocabulary indexed without a fallback");
    checkTheCanonicalAnswer(test);
}

/**
 * @brief A boot that fails drops the mind before it: asking it something then fails instead of reading weights out of
 *        an arena the failed boot may have given back, and the next boot that fits brings a mind up again.
 *
 * @details The boot fails on a region whose copy of the image would pass the address space, which it refuses before
 *          claiming anything: a request the heap cannot serve drains it (#868).
 */
LPL_TEST(a_boot_that_fails_leaves_no_mind_to_ask)
{
    const ModelSlot_t empty{};
    lpl::core::u8 *const image = gWritableModule + kOffWordBoundary;
    const lpl::core::u32 size = writeParityImage(image, sizeof gWritableModule - kOffWordBoundary);
    const ModelSlot_t copied = loadedSlot(image, size);
    libassistant_dialogue_result_t dialogue{};

    test.check(size != 0u, "the parity model's image is written two bytes past a word boundary");
    test.check(libassistant_boot_from_slot(0u, &empty), "a mind boots on its seed");
    test.check(!libassistant_boot_from_slot(SIZE_MAX, &copied),
               "a second boot, whose region and copy would pass the address space, fails");
    test.check(libassistant_model_index_fallbacks() == KERNEL_MODEL_SLOT_NOT_COUNTED,
               "and counts no fallback of an index, since it indexed no vocabulary");
    test.check(!libassistant_dialogue_round_trip(&dialogue), "and leaves no mind to ask");
    test.check(libassistant_boot_from_slot(0u, &empty), "a boot that fits brings one up again");
    checkTheCanonicalAnswer(test);
}

/**
 * @brief A boot asking for an arena no address space holds fails, and the next boot that fits brings the mind up: the
 *        heap used to hand that boot a block of no payload, which the mind took for 4 GiB and wrote past.
 */
LPL_TEST(a_boot_asking_for_an_arena_past_the_address_space_fails)
{
    const ModelSlot_t empty{};

    test.check(!libassistant_boot_from_slot(SIZE_MAX, &empty), "a boot asking for an arena of SIZE_MAX fails");
    test.check(libassistant_boot_from_slot(0u, &empty), "a boot that fits brings the mind up again");
    checkTheCanonicalAnswer(test);
}
