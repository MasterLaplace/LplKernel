/**
 * @file p16_agency_smoke.cpp
 * @brief Gate P16 `agency` — one turn of thought, two machines, one transcript.
 *
 * The floor above P14. That gate proves the demon COMPUTES the same thing on both
 * targets; this one proves it DECIDES the same thing — which note it kept when its
 * memory filled, which action it reached for first, whether it finished or asked for
 * help, and what the turn cost it.
 *
 * The two are separable and both are needed. Identical arithmetic with a different
 * eviction rule gives two demons that remember different pasts from one life; identical
 * decisions over different arithmetic is not something this project can even build.
 *
 * Nothing here runs a model, and that is what makes it a gate at all. A turn driven by
 * inference against a live world cannot be replayed, so the canonical case pairs
 * `DeterministicReasoner` with `ParityWorld` — both real policies, neither a stub — and
 * exercises the exact seam a model plugs into on the way past.
 *
 * Must match LplAssistant/tests/test_agency_parity.cpp on the host, bit for bit.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include "libassistant/libassistant.h"

#include <lpl/mind/Parity.hpp>

extern "C" void libassistant_agency_fold(libassistant_agency_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libassistant_agency_fold_result_t{};

    lpl::mind::AgencyFoldResult folded{};
    lpl::mind::foldAgency(folded);

    out->persona_sig = folded.personaSignature;
    out->intent_sig = folded.intentSignature;
    out->memory_sig = folded.memorySignature;
    out->recall_sig = folded.recallSignature;
    out->transcript_sig = folded.transcriptSignature;
    out->utterance_sig = folded.utteranceSignature;
    out->budget_sig = folded.budgetSignature;
    out->intent_kind = folded.intentKind;
    out->dropped = folded.droppedBytes;
    out->notes_held = folded.notesHeld;
    out->evictions = folded.evictions;
    out->refusals = folded.refusals;
    out->recall_hits = folded.recallHits;
    out->lines = folded.transcriptLines;
    out->steps = folded.stepsSpent;
    out->tokens = folded.tokensSpent;
    out->utterance_kind = folded.utteranceKind;
    out->satisfied = folded.worldSatisfied;
    out->world_refusals = folded.worldRefusals;
}
