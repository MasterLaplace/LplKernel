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
