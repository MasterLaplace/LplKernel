/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
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
 * @brief Which LplAssistant this libassistant was compiled from, as one telemetry record, for instance
 *        `[LPLTLM] mind version=0.1.0 commit=be3d999`.
 *
 * @return A string with static storage, ending in a newline. The version comes from
 *         LplAssistant's lplassistant/config.h, the commit is stamped by the build.
 */
const char *libassistant_identity_telemetry(void);

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
