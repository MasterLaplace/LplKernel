/**
 * @file p18_corpus_smoke.cpp
 * @brief Gate P18 `corpus` — a corpus that survives being written down.
 *
 * This file was `p12_knowledge_smoke.cpp`, and the rename is the convention rather than
 * tidying: gates are numbered in DELIVERY order and never reserved in advance. `rosetta`
 * took P12, `history` shipped inside libengine as P13, then `mind` P14, `satellite` P15,
 * `agency` P16, `reasoning` P17 — so the reader of a knowledge image is P18, claimed now
 * that it exists.
 *
 * What it folds is a TRANSLATION and not a computation, which makes it unlike every gate
 * before it. The others compile one arithmetic twice and check that the compiler did not
 * change it. This one takes the canonical corpus of gate P13, has it written to bytes by a
 * host tool, reads those bytes back here in ring 0, and rebuilds the history from what came
 * back. Three of its signatures must equal P13's own.
 *
 * An image that opened cleanly and had quietly rounded one confidence would pass every check
 * except those three — and the consequence of that rounding is a different consensus about
 * how a king died. So the equality with P13 IS the gate, and everything else it reports is
 * context for reading a failure.
 *
 * @author MasterLaplace
 * @copyright MIT License
 */

#include "libknowledge/libknowledge.h"

#include <lpl/knowledge/Parity.hpp>
#include <lpl/knowledge/ParityKnowBlob.hpp>

extern "C" void libknowledge_corpus_fold(libknowledge_corpus_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libknowledge_corpus_fold_result_t{};

    lpl::knowledge::KnowledgeFoldResult fold{};
    lpl::knowledge::foldKnowledgeState(lpl::knowledge::kParityKnowledgeImage,
                                       lpl::knowledge::kParityKnowledgeImageSize, fold);

    out->image_sig = fold.imageSignature;
    out->fact_sig = fold.factSignature;
    out->vocab_sig = fold.vocabularySignature;
    out->audit_sig = fold.auditSignature;
    out->page_sig = fold.pageSignature;
    out->citation_sig = fold.citationSignature;

    out->timeline_sig = fold.timelineSignature;
    out->chronicle_sig = fold.chronicleSignature;
    out->minority_sig = fold.minoritySignature;

    out->image_bytes = fold.imageBytes;
    out->open_status = fold.openStatus;
    out->sections = fold.sections;
    out->skipped = fold.skipped;
    out->facts = fold.facts;
    out->sources = fold.sources;
    out->documents = fold.documents;
    out->loci = fold.loci;
    out->names = fold.vocabulary;
    out->matched = fold.queryMatched;
    out->returned = fold.queryReturned;
    out->truncated = fold.queryTruncated;
    out->consensus = fold.consensusObject;
    out->provenance = fold.provenanceOk;
    out->round_trip = fold.roundTrip;
    out->rejected = fold.decodeRejected;
}
