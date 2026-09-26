/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file libknowledge.h
 * @brief C facade over the demon's memory, linked into the kernel.
 *
 * The kernel is C; the module behind this header is C++. One narrow extern "C"
 * surface keeps that boundary honest, the same way libengine.h does — plain
 * structs, no ownership crossing, no exceptions, and every entry point safe to
 * call from a context that must not block.
 *
 * What this library is FOR, stated once so nobody has to infer it: `lpl::history`
 * (inside libengine, gate P13) owns the arithmetic of doubt — trust, fusion, the
 * demotion of a contradicted claim — and trades in IDENTIFIERS. This library owns
 * the other half: the `.lplknow` image, its bounded reader, and the identity that
 * turns a canonical name into one of those identifiers. `history/Fact.hpp` wrote
 * that division down itself.
 *
 * The image the kernel carries is `kParityKnowledgeImage`, a byte array in the tree,
 * for the same reason `ParityPackBlob.hpp` is one: a kernel build must require no host
 * tool, and a gate that needed a file present would be a gate that skips itself when
 * it is not. Loading a harvested image as a boot module is the right destination and
 * is deliberately not written yet: nothing produces one, so a loader would be a
 * reader with no writer.
 *
 * Validation is unconditional either way: magic, version, declared extent, content
 * hash. An invalid image is reported, never silently replaced by a built-in fallback;
 * a corrupt memory has to be visible as a corrupt memory.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef LIBKNOWLEDGE_H
#define LIBKNOWLEDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct libknowledge_corpus_fold_result_t
 * @brief Gate P18 `corpus` — the signatures the host oracle must equal.
 *
 * Plain words only, no Fixed32 and no bool, so the kernel copies it field by field
 * exactly as it does for every other fold result.
 *
 * Three of these fields are unlike anything in the earlier gates and are the reason
 * this one exists: `timeline_sig`, `chronicle_sig` and `minority_sig` must equal gate
 * P13's OWN values. P13 builds a history from a corpus held in memory; this builds the
 * same history from a corpus that was written to bytes and read back. An inequality
 * says the format lost something — and the something would be a different consensus
 * about how a king died, which is the failure the whole module exists to prevent.
 */
typedef struct {
    uint32_t image_sig;    /**< Fold of the image, byte for byte. */
    uint32_t fact_sig;     /**< Fold of every claim the image holds. */
    uint32_t vocab_sig;    /**< Fold of the names it carries. */
    uint32_t audit_sig;    /**< Fold of the provenance tally. */
    uint32_t page_sig;     /**< Fold of the canonical query's page. */
    uint32_t citation_sig; /**< Fold of the rendered citation of the cited claim. */

    uint32_t timeline_sig;  /**< MUST equal gate P13's timeline signature. */
    uint32_t chronicle_sig; /**< MUST equal gate P13's chronicle signature. */
    uint32_t minority_sig;  /**< MUST equal gate P13's minority signature. */

    uint32_t image_bytes; /**< Size of the image read. */
    uint32_t open_status; /**< 0 when the image was accepted; otherwise why not. */
    uint32_t sections;    /**< Sections the table declared. */
    uint32_t skipped;     /**< Sections whose type this reader does not know. */
    uint32_t facts;       /**< Claims the image holds. */
    uint32_t sources;     /**< Source profiles it describes. */
    uint32_t documents;   /**< Documents it describes. */
    uint32_t loci;        /**< Loci it describes. */
    uint32_t names;       /**< Identifiers it names. */
    uint32_t matched;     /**< Claims the canonical query matched. */
    uint32_t returned;    /**< Rows it returned under its cap. */
    uint32_t truncated;   /**< 1 when the cap bit. */
    uint32_t consensus;   /**< What the decoded corpus believes killed the king. */
    uint32_t provenance;  /**< 1 when every claim can be weighed. */
    uint32_t round_trip;  /**< 1 when the decoded corpus equals the authored one. */
    uint32_t rejected;    /**< Records the decoder refused. MUST be zero. */
} libknowledge_corpus_fold_result_t;

/**
 * @brief Opens the canonical image, queries it, and folds every stage.
 *
 * @details What it folds is a TRANSLATION and not a computation, which makes it unlike
 *          every gate before it: the canonical corpus of gate P13 is written to bytes by a
 *          host tool, read back here in ring 0, and the history rebuilt from what came
 *          back. The equality of three signatures with P13's own IS the gate; everything
 *          else it reports is context for reading a failure. Must match
 *          LplKnowledge/tests/test_knowledge_parity.cpp.
 *
 * @param out Receives the signatures.
 */
extern void libknowledge_corpus_fold(libknowledge_corpus_fold_result_t *out);

/**
 * @brief A word for where the image this library reads came from.
 *
 * One provenance today — "embedded" — and it is reported rather than assumed
 * because the second is coming: an image harvested on a host will arrive as a boot
 * module, and the day it does, "the gate read the built-in corpus" and "the gate read
 * what was booted" must be distinguishable in a log. `kernel_model_slot_state_text`
 * exists for the same reason on the weights side.
 *
 * @note The image is validated before it is called embedded, not merely found present. An
 *       image the kernel carries can still be wrong — a hand-edited blob, a half-applied
 *       patch — and reporting "embedded" for one that does not open would be a silent
 *       fallback. The answers stay apart on purpose, exactly as the weights slot keeps
 *       absent, malformed and loaded apart.
 *
 * @return "embedded" when the image opens, "malformed" when it does not; "module" once one
 *         can be booted.
 */
extern const char *libknowledge_image_state(void);

/**
 * @brief Bytes of the image this library reads.
 *
 * @return Its size.
 */
extern uint32_t libknowledge_image_bytes(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBKNOWLEDGE_H */
