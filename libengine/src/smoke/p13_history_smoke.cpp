#include "libengine/libengine.h"

#include <lpl/history/Parity.hpp>

extern "C" void libengine_history_fold(libengine_history_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_history_fold_result_t{};

    lpl::history::HistoryFoldResult folded{};
    lpl::history::foldHistoryState(folded);

    out->timeline_sig = folded.timelineSignature;
    out->chronicle_sig = folded.chronicleSignature;
    out->minority_sig = folded.minoritySignature;
    out->constraints = folded.constraints;
    out->contradictions = folded.contradictions;
    out->demoted = folded.demoted;
    out->consensus_object = folded.consensusObject;
    out->minority_reachable = folded.minorityReachable;
    out->scored = folded.scoredClaims;
    out->earned = folded.earned;
}
