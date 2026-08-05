/**
 * @file p14_mind_smoke.cpp
 * @brief Gate P14 `mind` — the demon thinks identically in ring 0.
 *
 * Same weights, same prompt, same seed as the host oracle; fold the emitted
 * tokens and require the signatures to match bit for bit. Until this passes,
 * "the assistant runs on the kernel" is an intention rather than a fact.
 *
 * The weights are DERIVED here, not loaded, and that is what makes this a gate about
 * arithmetic rather than about file I/O. Every stage is folded separately — the
 * quantised tensors, the tokenised prompt, the scores, the residual stream, the freely
 * sampled tokens and the grammar-constrained ones — so a mismatch names the layer
 * that moved instead of only saying the answer changed.
 *
 * The gate re-carves the tensor arena from zero, so it drops the live mind first.
 *
 * Must match LplAssistant/tests/test_infer_parity.cpp on the host, bit for bit.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include "libassistant/libassistant.h"

#include <kernel/ai/tensor_arena.h>

#include <lpl/infer/Parity.hpp>

extern "C" void libassistant_mind_fold(libassistant_mind_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libassistant_mind_fold_result_t{};

    libassistant_shutdown();

    // The kernel's region when there is one, the allocator otherwise. Which of the
    // two was used changes nothing below it: TensorArena adopts a block it does not
    // own with the same bump logic it uses for one it does, which is exactly why the
    // arena byte count is worth folding at all.
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
