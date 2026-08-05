/**
 * @file p17_reasoning_smoke.cpp
 * @brief Gate P17 `reasoning` — the demon in ring 0 decides by generation.
 *
 * The three agency gates read as a sentence. P14 proves the kernel COMPUTES the same
 * forward pass as the host. P16 proves it DECIDES the same turn when a rule is doing
 * the deciding. This one puts the model back in the chair: the same turn, but every
 * move chosen by a transformer running in ring 0, under a grammar rebuilt from the
 * world at each step.
 *
 * The claim is not that the model is good — its weights come from a seed and it has
 * learned nothing. It is that a model which has learned nothing STILL cannot name an
 * action the world did not offer, because the grammar makes the wrong answer
 * unspellable rather than merely unlikely. That is a property of the language, so it
 * does not weaken as the model shrinks, which is the entire reason inference sits
 * behind a grammar here instead of behind a validator.
 *
 * `free_legal` is what makes it a measurement rather than a reassurance: the same
 * weights, the same seeds, generating unconstrained, and how often they land on
 * something the world would have accepted. A zero in `illegal` alone would also be
 * satisfied by a demon that never acted at all.
 *
 * Must match LplAssistant/tests/test_agency_parity.cpp on the host, bit for bit.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include "libassistant/libassistant.h"

#include <kernel/ai/tensor_arena.h>

#include <lpl/mind/Parity.hpp>

extern "C" void libassistant_reasoning_fold(libassistant_reasoning_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libassistant_reasoning_fold_result_t{};

    /* The live mind is dropped first. This gate re-carves the arena from the base, so
       anything still holding weights handed out of it would be reading storage that
       has been given away — not a crash, but a wrong answer several layers later. */
    libassistant_shutdown();

    void *const region = kernel_tensor_arena_ready() ? kernel_tensor_arena_base() : nullptr;
    const lpl::core::usize bytes = kernel_tensor_arena_ready() ? kernel_tensor_arena_size() : 0u;

    lpl::mind::ReasoningFoldResult folded{};
    lpl::mind::foldReasoning(folded, region, bytes);

    out->transcript_sig = folded.transcriptSignature;
    out->action_sig = folded.actionSignature;
    out->utterance_sig = folded.utteranceSignature;
    out->generations = folded.generations;
    out->completions = folded.completions;
    out->illegal = folded.illegalActions;
    out->exhausted = folded.grammarExhausted;
    out->tokens = folded.tokensGenerated;
    out->lines = folded.transcriptLines;
    out->steps = folded.stepsSpent;
    out->satisfied = folded.satisfied;
    out->free_attempts = folded.freeAttempts;
    out->free_legal = folded.freeLegalNames;
    out->arena_bytes = folded.arenaBytes;
}
