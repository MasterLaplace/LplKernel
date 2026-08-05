/**
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
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

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
 * @param out Receives the signatures.
 */
extern void libknowledge_corpus_fold(libknowledge_corpus_fold_result_t *out);

/**
 * @brief A word for where the image this library reads came from.
 *
 * Only one answer today — "embedded" — and it is reported rather than assumed
 * because the second answer is coming: an image harvested on a host will arrive as a
 * boot module, and the day it does, "the gate read the built-in corpus" and "the gate
 * read what was booted" must be distinguishable in a log. `kernel_model_slot_state_text`
 * exists for the same reason on the weights side.
 *
 * @return "embedded", or "module" once one can be booted.
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
