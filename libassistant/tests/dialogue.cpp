#include "libassistant/libassistant.h"

#include <kernel/ai/tensor_arena.h>

#include <lpl/infer/Parity.hpp>
#include <lpl/testing/Test.hpp>

LPL_TEST_SUITE(dialogue);

namespace {

[[nodiscard]] bool sameText(const char *lhs, const char *rhs)
{
    while (*lhs != '\0' && *lhs == *rhs)
    {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
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
