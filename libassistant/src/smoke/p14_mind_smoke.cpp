#include "libassistant/libassistant.h"

#include <kernel/ai/tensor_arena.h>

#include <lpl/infer/Parity.hpp>

extern "C" void libassistant_mind_fold(libassistant_mind_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libassistant_mind_fold_result_t{};

    libassistant_shutdown();

    void *const region = kernel_tensor_arena_ready() ? kernel_tensor_arena_base() : nullptr;
    const lpl::core::usize bytes = kernel_tensor_arena_ready() ? kernel_tensor_arena_size() : 0u;

    lpl::infer::MindFoldResult folded{};
    lpl::infer::foldMindState(folded, region, bytes);

    out->weight_sig = folded.weightSignature;
    out->prompt_sig = folded.promptSignature;
    out->logit_sig = folded.logitSignature;
    out->residual_sig = folded.residualSignature;
    out->token_sig = folded.tokenSignature;
    out->constrained_sig = folded.constrainedSignature;
    out->text_sig = folded.textSignature;
    out->vocab = folded.vocabSize;
    out->prompt_tokens = folded.promptTokens;
    out->generated = folded.generated;
    out->draws = folded.draws;
    out->constrained_tokens = folded.constrainedTokens;
    out->admitted_first = folded.admittedFirst;
    out->grammar_complete = folded.grammarComplete;
    out->forbidden = folded.forbiddenReachable;
    out->blob_bytes = folded.blobBytes;
    out->blob_reopened = folded.blobReopened;
    out->arena_bytes = folded.arenaBytes;

    kernel_tensor_arena_record_used(folded.arenaBytes);
}
