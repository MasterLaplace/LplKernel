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
