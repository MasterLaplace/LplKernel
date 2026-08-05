/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** History parity fold — two sources, one past.
**
** procgen builds a plausible world, ecology makes populations live in it, ai makes
** agents act. A HISTORY is that simulation run over centuries under dated constraints
** — the graph does not add an engine, it adds constraints, and two sets of them are
** two Worlds of one Server.
**
** What this gate folds is the part that must not drift: a confidence in Fixed32
** decides which of two contradictory claims becomes the consensus view. A rounding
** that differed between targets would give two histories from one corpus.
**
** Must match tests/parity/test_history_parity.cpp on the host, bit for bit.
*/
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
