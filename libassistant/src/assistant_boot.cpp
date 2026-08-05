/**
 * @file assistant_boot.cpp
 * @brief Bringing the demon up inside the kernel, server profile only.
 *
 * Mirrors client_app.cpp: build the real objects, not a throwaway facade. The
 * client image constructs an Engine and a World; the server image constructs those
 * plus a mind, and hands it the budget the display would otherwise have taken.
 *
 * Ordering matters and is the usual trap: the tensor arena must exist before any
 * model is touched, and nothing here may run from a global constructor, because
 * those execute before the kernel has a heap. Every long-lived object therefore
 * lives in raw storage and is placement-constructed by @ref libassistant_boot —
 * the same fix a global `Registry` needed the first time it crashed in init_array.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include "libassistant/libassistant.h"

#include <kernel/ai/inference_budget.h>
#include <kernel/ai/model_slot.h>
#include <kernel/ai/tensor_arena.h>
#include <kernel/dialogue/dialogue_channel.h>

#include <lpl/infer/Inference.hpp>
#include <lpl/infer/Parity.hpp>
#include <lpl/infer/Tokenizer.hpp>

#include <new>

namespace {

constexpr lpl::core::u32 kFnv1aOffsetBasis = 0x811C9DC5u;
constexpr lpl::core::u32 kFnv1aPrime = 0x01000193u;

/**
 * @brief Folds one word into a running FNV-1a hash.
 * @param hash Running value.
 * @param word Word to absorb.
 */
void foldWord(lpl::core::u32 &hash, lpl::core::u32 word) noexcept { hash = (hash ^ word) * kFnv1aPrime; }

/**
 * @brief Length of a null-terminated string.
 * @param text The string.
 * @return Its length.
 */
lpl::core::u32 textLength(const char *text) noexcept
{
    lpl::core::u32 length = 0u;
    while (text[length] != '\0')
        ++length;
    return length;
}

/**
 * @struct MindState
 * @brief Everything the demon needs, in the order it needs it.
 */
struct MindState {
    lpl::infer::TensorArena arena;
    lpl::infer::Vocab vocab;
    lpl::infer::Model model;
    lpl::infer::Grammar grammar;
    lpl::infer::Inference inference;
    lpl::infer::Tokenizer tokenizer;

    /**
     * @brief Adopts the region the kernel reserved.
     * @param memory Region base.
     * @param bytes  Region size.
     */
    MindState(void *memory, lpl::core::usize bytes) noexcept : arena(memory, bytes) {}
};

alignas(alignof(MindState)) unsigned char gMindStorage[sizeof(MindState)];
MindState *gMind = nullptr;
ModelSlot_t gSlot{};

/// Tokens one reply may cost. Small on purpose: a call is a dozen bytes, and a
/// budget sized for prose would never be the thing that reported a runaway.
constexpr lpl::core::u32 kReplyTokenBudget = 24u;

} // namespace

extern "C" size_t libassistant_recommended_arena_bytes(void)
{
    return static_cast<size_t>(lpl::infer::parityArenaBytes());
}

extern "C" const char *libassistant_model_slot_state(void) { return kernel_model_slot_state_text(&gSlot); }

extern "C" bool libassistant_boot(size_t arena_bytes)
{
    if (arena_bytes == 0u)
        arena_bytes = libassistant_recommended_arena_bytes();

    if (!kernel_tensor_arena_initialize(arena_bytes))
        return false;

    /* A module is a legitimate absence, not a failure: this image derives its
       weights from a seed. What would be a failure is quietly deriving them after
       being handed an image that did not parse, so the two are reported apart. */
    (void) kernel_model_slot_probe(&gSlot);

    gMind = new (gMindStorage) MindState{kernel_tensor_arena_base(), kernel_tensor_arena_size()};

    if (!lpl::infer::buildParityVocab(gMind->arena, gMind->vocab))
    {
        gMind = nullptr;
        return false;
    }

    lpl::infer::ModelConfig shape = lpl::infer::parityModelConfig();
    shape.vocabSize = gMind->vocab.size();
    if (!gMind->model.synthesise(gMind->arena, shape, gMind->vocab, lpl::infer::parityModelSeed()))
    {
        gMind = nullptr;
        return false;
    }

    lpl::core::u32 phraseCount = 0u;
    const char *const *phrases = lpl::infer::parityGrammarPhrases(phraseCount);
    if (!gMind->grammar.build(gMind->arena, phrases, phraseCount))
    {
        gMind = nullptr;
        return false;
    }

    if (!gMind->inference.initialise(gMind->arena, gMind->model, lpl::infer::CachePolicy::Refuse))
    {
        gMind = nullptr;
        return false;
    }

    gMind->tokenizer = lpl::infer::Tokenizer{gMind->vocab};

    kernel_tensor_arena_record_used(gMind->arena.used());
    kernel_dialogue_channel_reset();
    kernel_inference_budget_open(kReplyTokenBudget);
    return true;
}

extern "C" bool libassistant_dialogue_round_trip(libassistant_dialogue_result_t *out)
{
    if (out == nullptr)
        return false;
    *out = libassistant_dialogue_result_t{};
    if (gMind == nullptr)
        return false;

    kernel_dialogue_channel_reset();
    kernel_inference_budget_open(kReplyTokenBudget);

    // The sovereign speaks. One byte at a time through the single-producer ring,
    // because that is how the words will really arrive — from a console handler or a
    // capture buffer, in a context that cannot block.
    const char *const question = lpl::infer::parityPrompt();
    const lpl::core::u32 questionBytes = textLength(question);
    for (lpl::core::u32 i = 0u; i < questionBytes; ++i)
        if (kernel_dialogue_channel_offer_to_demon(static_cast<uint8_t>(question[i])))
            ++out->question_bytes;

    // The demon listens.
    char heard[128];
    while (out->consumed < 128u)
    {
        uint8_t byte = 0u;
        if (!kernel_dialogue_channel_take_for_demon(&byte))
            break;
        heard[out->consumed++] = static_cast<char>(byte);
    }

    lpl::core::u32 tokens[64];
    lpl::core::u32 skipped = 0u;
    const lpl::core::u32 count = gMind->tokenizer.encode(heard, out->consumed, tokens, 64u, skipped);

    lpl::infer::GenerationParams params{};
    params.sampler.topK = 4u;
    params.sampler.seed = lpl::infer::paritySamplerSeed();
    params.maxTokens = kReplyTokenBudget;

    lpl::core::u32 emitted[32];
    lpl::infer::GenerationReport report{};
    if (!gMind->inference.generate(tokens, count, params, &gMind->grammar, emitted, 32u, report))
        return false;

    // Every token costs one from the budget. Claimed after the fact rather than
    // before, because the generation is already bounded by maxTokens — this counts
    // what was spent, it does not second-guess the ceiling.
    for (lpl::core::u32 i = 0u; i < report.generated; ++i)
        (void) kernel_inference_budget_claim();

    char answer[128];
    const lpl::core::u32 answerBytes = gMind->tokenizer.decode(emitted, report.generated, answer, 128u);
    for (lpl::core::u32 i = 0u; i < answerBytes; ++i)
        if (kernel_dialogue_channel_offer_to_sovereign(static_cast<uint8_t>(answer[i])))
            ++out->answer_bytes;

    // The sovereign reads. A channel only one end ever touches proves nothing.
    char delivered[128];
    lpl::core::u32 signature = kFnv1aOffsetBasis;
    while (out->delivered < 128u)
    {
        uint8_t byte = 0u;
        if (!kernel_dialogue_channel_take_for_sovereign(&byte))
            break;
        delivered[out->delivered] = static_cast<char>(byte);
        foldWord(signature, static_cast<lpl::core::u32>(byte));
        ++out->delivered;
    }
    foldWord(signature, out->delivered);
    out->answer_sig = signature;

    lpl::core::u32 phraseCount = 0u;
    const char *const *phrases = lpl::infer::parityGrammarPhrases(phraseCount);
    for (lpl::core::u32 p = 0u; p < phraseCount; ++p)
    {
        if (textLength(phrases[p]) != out->delivered)
            continue;
        bool same = true;
        for (lpl::core::u32 i = 0u; same && i < out->delivered; ++i)
            same = delivered[i] == phrases[p][i];
        if (same)
            out->valid_call = 1u;
    }

    out->dropped = kernel_dialogue_channel_dropped();
    out->budget_spent = kernel_inference_budget_spent();
    out->budget_denied = kernel_inference_budget_denied();
    kernel_tensor_arena_record_used(gMind->arena.used());
    return out->valid_call == 1u;
}

extern "C" void libassistant_shutdown(void) { gMind = nullptr; }
