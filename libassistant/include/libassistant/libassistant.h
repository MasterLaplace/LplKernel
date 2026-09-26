/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file libassistant.h
 * @brief C facade over the demon's mind, linked into the kernel.
 *
 * The kernel is C; the module behind this header is C++. One narrow extern "C"
 * surface keeps that boundary honest, the same way libengine.h does — plain
 * structs, no ownership crossing, no exceptions, and every entry point safe to
 * call from a context that must not block.
 *
 * Ordering matters and is the usual trap: the tensor arena must exist before any
 * model is touched, and nothing may run from a global constructor, because those
 * execute before the kernel has a heap. Every long-lived object of the mind therefore
 * lives in raw storage and is placement-constructed by libassistant_boot — the same
 * fix a global `Registry` needed the first time it crashed in init_array.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef LIBASSISTANT_H
#define LIBASSISTANT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct libassistant_mind_fold_result_t
 * @brief Gate P14 `mind` — the signatures the host oracle must equal.
 *
 * Plain words only, no Fixed32 and no bool, so the kernel copies it field by field
 * exactly as it does for every other fold result.
 */
typedef struct {
    uint32_t weight_sig;         /**< Fold of every quantised tensor. */
    uint32_t prompt_sig;         /**< Fold of the tokenised prompt. */
    uint32_t logit_sig;          /**< Fold of the scores at the last position. */
    uint32_t residual_sig;       /**< Fold of the residual stream at the last position. */
    uint32_t token_sig;          /**< Fold of the freely sampled tokens. */
    uint32_t constrained_sig;    /**< Fold of the grammar-constrained tokens. */
    uint32_t text_sig;           /**< Fold of the bytes those tokens decode to. */
    uint32_t vocab;              /**< Tokens in the canonical table. */
    uint32_t prompt_tokens;      /**< Tokens the prompt segmented into. */
    uint32_t generated;          /**< Tokens the free run produced. */
    uint32_t draws;              /**< Random words the sampler consumed. */
    uint32_t constrained_tokens; /**< Tokens the constrained run produced. */
    uint32_t admitted_first;     /**< Tokens the grammar allowed at its first step. */
    uint32_t grammar_complete;   /**< 1 when the constrained run spelled a whole call. */
    uint32_t forbidden;          /**< 1 when a forbidden call was reachable — a failure. */
    uint32_t blob_bytes;         /**< Size of the written weights image. */
    uint32_t blob_reopened;      /**< 1 when that image read back to the same weights. */
    uint32_t arena_bytes;        /**< Bytes the whole run carved out of the arena. */
} libassistant_mind_fold_result_t;

/**
 * @brief Runs the canonical mind case and folds every stage of it.
 *
 * @details Same weights, same prompt, same seed as the host oracle, and the emitted
 *          tokens folded — until this matches, "the assistant runs on the kernel" is an
 *          intention rather than a fact. The weights are DERIVED, not loaded, which makes
 *          this a gate about arithmetic rather than about file I/O. Every stage is folded
 *          separately — the quantised tensors, the tokenised prompt, the scores, the
 *          residual stream, the freely sampled tokens and the grammar-constrained ones —
 *          so a mismatch names the layer that moved instead of only saying the answer
 *          changed. Must match LplAssistant/tests/test_infer_parity.cpp.
 *
 * Runs inside the kernel's tensor arena when one has been claimed, and claims a
 * block from the allocator otherwise. Which of the two it used changes nothing it
 * reports: TensorArena adopts a block it does not own with the same bump logic it uses
 * for one it does, and only the block's provenance differs — which is exactly why the
 * arena byte count is worth folding at all.
 *
 * @note The live mind is dropped first, since the run re-carves the arena from its base.
 *
 * @param out Receives the signatures.
 */
extern void libassistant_mind_fold(libassistant_mind_fold_result_t *out);

/**
 * @struct libassistant_dialogue_result_t
 * @brief What one exchange through the aperture did.
 */
typedef struct {
    uint32_t question_bytes; /**< Bytes the sovereign put into the channel. */
    uint32_t consumed;       /**< Bytes the demon took out of it. */
    uint32_t answer_bytes;   /**< Bytes the demon put back. */
    uint32_t delivered;      /**< Bytes the sovereign read back. */
    uint32_t dropped;        /**< Bytes lost to a full ring. */
    uint32_t budget_spent;   /**< Tokens the reply cost. */
    uint32_t budget_denied;  /**< Claims refused because the budget was empty. */
    uint32_t answer_sig;     /**< Fold of the delivered bytes. */
    uint32_t valid_call;     /**< 1 when the reply is a whole call the engine can run. */
} libassistant_dialogue_result_t;

/**
 * @brief Puts a question through the aperture and reads the answer back out.
 *
 * The whole seam exercised end to end: bytes in through the single-producer ring,
 * a grammar-constrained generation inside a token budget, bytes back out. It is
 * deliberately a round trip and not a direct call — a channel that is never read
 * from both ends is a channel nothing proves.
 *
 * @param out Receives what happened.
 * @return true when a whole, valid call came back.
 */
extern bool libassistant_dialogue_round_trip(libassistant_dialogue_result_t *out);

/**
 * @brief Brings the mind up: arena, slot, weights, budget.
 *
 * Nothing here may run from a global constructor — those execute before the kernel
 * has a heap, and the arena is the largest allocation the kernel ever makes.
 *
 * @note A missing model module is a legitimate absence, not a failure: this image derives
 *       its weights from a seed. What would be a failure is quietly deriving them after being
 *       handed an image that did not parse, so the two are reported apart by
 *       libassistant_model_slot_state.
 *
 * @param arena_bytes Region to claim for weights, cache and scratch.
 * @return true when the mind is ready to be asked something.
 */
extern bool libassistant_boot(size_t arena_bytes);

/**
 * @brief Drops the live mind without releasing the region.
 *
 * Anything that re-carves the arena has to call this first. A mind whose weights
 * have been handed out again is not a crash, it is a wrong answer three layers
 * later — which is strictly worse.
 */
extern void libassistant_shutdown(void);

/**
 * @struct libassistant_satellite_fold_result_t
 * @brief Gate P15 `satellite` — the decisions three machines must share.
 *
 * A hosted development node, this kernel's satellite profile and an eventual
 * microcontroller firmware share no register, no allocator and no instruction set.
 * What they must share is every decision about the same audio: when to start sending,
 * when to stop, whether the word was heard, and whether the node is hearing itself.
 */
typedef struct {
    uint32_t feature_sig;     /**< Fold of every frame's spectral shape. */
    uint32_t level_sig;       /**< Fold of every frame's root-mean-square level. */
    uint32_t event_sig;       /**< Fold of the voice-activity decisions. */
    uint32_t wire_sig;        /**< Fold of every datagram the node emitted. */
    uint32_t state_sig;       /**< Fold of the power state after each frame. */
    uint32_t template_sig;    /**< Fold of the armed wake-word template. */
    uint32_t emitted;         /**< Audio datagrams sent. */
    uint32_t utterances;      /**< Utterances the detector closed. */
    uint32_t detections;      /**< Times the wake word matched. */
    uint32_t wake_frame;      /**< Frame at which it first matched. */
    uint32_t wake_distance;   /**< Distance at that match. */
    uint32_t speech_distance; /**< Closest ordinary speech came to the template. */
    uint32_t echoes;          /**< Captures identified as the node's own voice. */
    uint32_t transitions;     /**< Power state changes. */
    uint32_t idle_permille;   /**< Share of the timeline spent idle. */
    uint32_t duty_permille;   /**< Share of the timeline the processor was awake. */
    uint32_t tagged_audio;    /**< 1 when audio beginning "TXT:" still decodes as audio. */
} libassistant_satellite_fold_result_t;

/**
 * @brief Runs the canonical satellite exchange and folds every stage of it.
 *
 * @details What the gate guards is not an algorithm but an AGREEMENT: three machines,
 *          two of which will not even be x86, deciding the same thing about the same
 *          audio. The audio is synthesised from the frame index alone, so no wave file
 *          has to reach both sides — the same reason the world gate derives a world from
 *          a seed rather than loading one. Must match
 *          LplAssistant/tests/test_satellite_parity.cpp.
 *
 * @param out Receives the signatures.
 */
extern void libassistant_satellite_fold(libassistant_satellite_fold_result_t *out);

/**
 * @struct libassistant_agency_fold_result_t
 * @brief Gate P16 `agency` — one turn of thought, folded stage by stage.
 *
 * Plain words only, like every other fold result here, so the kernel copies it field
 * by field. What it carries is a SEQUENCE OF DECISIONS rather than a computation:
 * which note survived a full memory, which action was reached for first, whether the
 * turn ended in an answer or a question, and what it cost.
 */
typedef struct {
    uint32_t persona_sig;    /**< Fold of who was thinking. */
    uint32_t intent_sig;     /**< Fold of the parsed utterance. */
    uint32_t memory_sig;     /**< Fold of the store after it had to choose. */
    uint32_t recall_sig;     /**< Fold of what the lookup surfaced. */
    uint32_t transcript_sig; /**< Fold of every line of the turn. */
    uint32_t utterance_sig;  /**< Fold of what was said to the sovereign. */
    uint32_t budget_sig;     /**< Fold of what the turn cost. */
    uint32_t intent_kind;    /**< How the utterance was classified. */
    uint32_t dropped;        /**< Bytes the parser refused as unacceptable. */
    uint32_t notes_held;     /**< Notes left in the store. */
    uint32_t evictions;      /**< Notes displaced to make room. */
    uint32_t refusals;       /**< Notes turned away as too trivial to keep. */
    uint32_t recall_hits;    /**< Notes the lookup returned. */
    uint32_t lines;          /**< Lines the turn produced. */
    uint32_t steps;          /**< Reason-act-observe rounds it took. */
    uint32_t tokens;         /**< Tokens it cost. */
    uint32_t utterance_kind; /**< 0 answer, 1 ask, 2 report. */
    uint32_t satisfied;      /**< 1 when the world says the job was done. */
    uint32_t world_refusals; /**< Actions the world turned down. */
} libassistant_agency_fold_result_t;

/**
 * @brief Runs the canonical turn of thought and folds every stage of it.
 *
 * @details The floor above P14. That gate proves the demon COMPUTES the same thing on
 *          both targets; this one proves it DECIDES the same thing. The two are separable
 *          and both are needed: identical arithmetic with a different eviction rule gives
 *          two demons that remember different pasts from one life.
 *
 * @note Nothing here runs a model, and that is what makes it a gate at all: a turn
 *       driven by inference against a live world cannot be replayed. The canonical case
 *       pairs `DeterministicReasoner` with `ParityWorld` — both real policies, neither a
 *       stub — and exercises the exact seam a model plugs into on the way past. Must
 *       match LplAssistant/tests/test_agency_parity.cpp.
 *
 * @param out Receives the signatures.
 */
extern void libassistant_agency_fold(libassistant_agency_fold_result_t *out);

/**
 * @struct libassistant_reasoning_fold_result_t
 * @brief Gate P17 `reasoning` — the same turn, decided by a model instead of a rule.
 *
 * Two fields carry the claim and must be read together. `illegal` is what the grammar
 * makes impossible and must be zero; `free_legal` is the control — the SAME model,
 * generating with no constraint, and how often it happened to name something the world
 * would accept. Zero in the first alone proves nothing: a demon that never acts also
 * never acts illegally.
 */
typedef struct {
    uint32_t transcript_sig; /**< Fold of the model-driven turn. */
    uint32_t action_sig;     /**< Fold of the chosen actions alone. */
    uint32_t utterance_sig;  /**< Fold of what was said at the end. */
    uint32_t generations;    /**< Decisions that went to the model. */
    uint32_t completions;    /**< Decisions that spelled a whole phrase. */
    uint32_t illegal;        /**< Actions the world did not offer. MUST be zero. */
    uint32_t exhausted;      /**< Steps where the language ran out. */
    uint32_t tokens;         /**< Tokens the model produced. */
    uint32_t lines;          /**< Lines the turn produced. */
    uint32_t steps;          /**< Rounds it took. */
    uint32_t satisfied;      /**< 1 when the world's goal was met. */
    uint32_t free_attempts;  /**< Unconstrained generations run as a control. */
    uint32_t free_legal;     /**< How many of those named a legal action. */
    uint32_t arena_bytes;    /**< Bytes carved out; a per-target fit check, not an invariant. */
} libassistant_reasoning_fold_result_t;

/**
 * @brief Runs the canonical turn with a real model behind the decisions.
 *
 * @details The same turn as the agency gate, but every move chosen by a transformer
 *          running in ring 0, under a grammar rebuilt from the world at each step. The
 *          claim is not that the model is good — its weights come from a seed and it has
 *          learned nothing. It is that a model which has learned nothing STILL cannot
 *          name an action the world did not offer, because the grammar makes the wrong
 *          answer unspellable rather than merely unlikely. That is a property of the
 *          language, so it does not weaken as the model shrinks. Must match
 *          LplAssistant/tests/test_agency_parity.cpp.
 *
 * @note The live mind is dropped first. This gate re-carves the arena from the base, so
 *       anything still holding weights handed out of it would be reading storage that has
 *       been given away — not a crash, but a wrong answer several layers later.
 *
 * @param out Receives the signatures.
 */
extern void libassistant_reasoning_fold(libassistant_reasoning_fold_result_t *out);

/**
 * @brief Bytes the canonical run wants in its arena.
 * @return The recommended region size.
 */
extern size_t libassistant_recommended_arena_bytes(void);

/**
 * @brief A word for what the weights module slot holds.
 * @return "empty", "malformed" or "loaded".
 */
extern const char *libassistant_model_slot_state(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBASSISTANT_H */
