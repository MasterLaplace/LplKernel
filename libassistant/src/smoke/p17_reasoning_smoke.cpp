#include "libassistant/libassistant.h"

#include <kernel/ai/tensor_arena.h>

#include <lpl/mind/Parity.hpp>

extern "C" void libassistant_reasoning_fold(libassistant_reasoning_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libassistant_reasoning_fold_result_t{};

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
