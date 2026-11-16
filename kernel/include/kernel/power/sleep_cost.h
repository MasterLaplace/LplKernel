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
 * @file sleep_cost.h
 * @brief What a sleep at each depth costs, measured on the package energy counter, and the depth a sleep that long
 *        should enter.
 *
 * A sleep of T at a depth costs E + P * T: P the package's power while the processor sleeps at that depth, E what one
 * sleep costs to enter and leave it, the time awake between two sleeps included. Firmware cannot give them: `_CST`
 * publishes a power per state, but across the 96 machines of the corpus list it is above 0 and falls with depth on 17,
 * and 39 machines from several vendors give the same 1000/200/0 mW. So each depth is measured: series of sleeps at it,
 * the package energy counter (kernel/power/energy_counter.h) read before and after each. Two series of different
 * sleep counts per second solve E and P, and a third checks them: a calibration whose check disagrees is refused, by
 * name, rather than trusted.
 *
 * The choice is then the depth with the least E + P * T, T being how long a sleep at that depth would last: each
 * depth's lead moves the wake-up earlier by a different amount. Where no depth's costs are measured (no counter, a
 * calibration refused), the choice falls to the rule of `_CST` residencies: the deepest depth whose residency the
 * sleep outlasts. Both are pure: the clock and the costs are parameters, so a test drives every case without sleeping.
 *
 * A session records the window of each sleep and the depth chosen for it, so that its sleeps replay against a sleep
 * always at the shallowest depth and one always at the deepest: what the choice saved, and the wake-ups a fixed depth
 * would have missed.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-14
 **************************************************************************/

#ifndef KERNEL_POWER_SLEEP_COST_H
#define KERNEL_POWER_SLEEP_COST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KERNEL_SLEEP_COST_SERIES      3u          /**< Series a calibration takes per depth: two solve, one checks. */
#define KERNEL_SLEEP_COST_CHECK_SHIFT 4u          /**< The check tolerates 1/2^4 of its energy past the quantisation. */
#define KERNEL_SLEEP_COST_SEPARATION  2u          /**< How many times one series' sleeps a second exceed the other's. */
#define KERNEL_SLEEP_COST_CYCLE_SHIFT 20u         /**< The comparison counts power in nanojoules per 2^20 cycles. */
#define KERNEL_SLEEP_COST_NO_DEPTH    0xFFFFFFFFu /**< What names no depth, outside every depth. */
#define KERNEL_SLEEP_COST_RECORDED    64u         /**< The latest sleeps a session's record keeps, a power of two. */
#define KERNEL_SLEEP_COST_DEPTHS      8u          /**< The most depths a replay takes. */

/**
 * @enum KernelSleepCostSource_t
 * @brief Where a depth's costs come from, and why they are not measured.
 */
typedef enum {
    KERNEL_SLEEP_COST_NOT_CALIBRATED = 0, /**< No calibration ran yet. */
    KERNEL_SLEEP_COST_NO_COUNTER = 1,     /**< The package energy counter is not present. */
    KERNEL_SLEEP_COST_MEASURED = 2,       /**< Measured, and the check series agreed. */
    KERNEL_SLEEP_COST_NOISY = 3,          /**< The check series disagreed past its tolerance, or a sleep entered another
                                               depth. */
    KERNEL_SLEEP_COST_UNSEPARATED = 4,    /**< The two series' sleeps a second are too alike to solve E and P. */
    KERNEL_SLEEP_COST_IMPLAUSIBLE = 5,    /**< A power of 0 or less or past 32 bits of microwatts, a sleep energy
                                               below 0 past the quantisation, or a series past what the solution
                                               holds. */
    KERNEL_SLEEP_COST_UNREAD = 6,         /**< A read of the counter failed during a series. */
    KERNEL_SLEEP_COST_UNTIMED = 7,        /**< A sleep of a series could not be armed on the timer. */
} KernelSleepCostSource_t;

/**
 * @brief One series of sleeps at one depth, as a calibration measured it.
 */
typedef struct KernelSleepCostSeries {
    uint32_t sleeps;      /**< Sleeps the series entered. */
    uint64_t nanoseconds; /**< From the read before its first sleep to the read after its last. */
    uint64_t nanojoules;  /**< The package's energy between those two reads. */
} KernelSleepCostSeries_t;

/**
 * @brief What a sleep at one depth costs.
 */
typedef struct KernelSleepCost {
    uint32_t source;                          /**< A KernelSleepCostSource_t; the values below hold when MEASURED. */
    uint32_t power_microwatts;                /**< P, the package's power while asleep at the depth. */
    uint64_t sleep_nanojoules;                /**< E, what one sleep costs beyond P: entering, leaving, the time
                                                   between two sleeps. */
    uint64_t power_nanojoules_per_mebicycles; /**< P in nanojoules per 2^20 cycles of the clock sleeps are timed on. */
} KernelSleepCost_t;

/**
 * @brief One depth as a sleep may take it.
 */
typedef struct KernelSleepCostCandidate {
    bool allowed;              /**< Whether the sleep may enter the depth: available, validated, learned, at most the
                                    caller's ceiling. */
    uint64_t asleep_cycles;    /**< How long a sleep at the depth would last: the plan's next wake-up on the depth's
                                    lead, less now; 0 when the lead does not fit. */
    uint64_t residency_cycles; /**< The depth's `_CST` residency, the rule's threshold. */
    KernelSleepCost_t cost;    /**< What a sleep at it costs. */
} KernelSleepCostCandidate_t;

/**
 * @brief One sleep as a session recorded it.
 */
typedef struct KernelSleepCostRecord {
    uint64_t window_cycles; /**< From the choice to the plan's next wake-up, before any depth's lead. */
    uint32_t depth;         /**< The depth chosen. */
    uint32_t by_cost;       /**< 1 when measured costs chose it, 0 when the rule did. */
} KernelSleepCostRecord_t;

/**
 * @brief One depth as a replay takes it.
 */
typedef struct KernelSleepCostReplayDepth {
    bool allowed;              /**< Whether the session's sleeps could take it. */
    uint64_t lead_cycles;      /**< How long before the wake-up a sleep at it is armed. */
    uint64_t residency_cycles; /**< Its `_CST` residency, the rule's threshold. */
    KernelSleepCost_t cost;    /**< What a sleep at it costs. */
} KernelSleepCostReplayDepth_t;

/**
 * @brief What a session's replay gives, against the shallowest and the deepest allowed depth taken for every sleep.
 */
typedef struct KernelSleepCostReplay {
    uint64_t chosen_cost;       /**< The sleeps at the depths chosen again for them, nanojoules times 2^20. */
    uint64_t shallowest_cost;   /**< The sleeps all at the shallowest allowed depth. */
    uint64_t deepest_cost;      /**< The sleeps all at the deepest allowed depth. */
    uint32_t chosen_misses;     /**< Sleeps the choice found no depth for. */
    uint32_t shallowest_misses; /**< Sleeps the shallowest depth's lead outlasts. */
    uint32_t deepest_misses;    /**< Sleeps the deepest depth's lead outlasts. */
    uint32_t agreements;        /**< Sleeps whose choice made again is the depth recorded. */
} KernelSleepCostReplay_t;

/**
 * @brief Solves E and P from the first two series and checks them on the third.
 *
 * @details With n sleeps over T nanoseconds and J nanojoules a series, J = n * E + P * T. The two first series solve
 *          it when their sleeps a second differ by KERNEL_SLEEP_COST_SEPARATION times at least; the third is
 *          predicted, and agrees when it misses by at most @p quantisation_nanojoules plus 1/2^4 of its energy. A
 *          sleep energy below 0 by no more than the quantisation per sleep of the first series is 0: a depth that
 *          costs nothing to enter, C1, measures so.
 *
 *          It solves in microseconds and microjoules, so that its products stay within 64 bits: a series
 *          under 1 us or past 2^24 us (16.8 s), of more than 2^16 sleeps or 2^32 uJ (4295 J), is
 *          IMPLAUSIBLE, as is a power past UINT32_MAX uW. Separated series never give a zero determinant in
 *          whole microseconds: rounding a length down loses less than 1 us, so equal products in microseconds would
 *          put two series of 1 us at least less than twice apart.
 *
 * @param series                  KERNEL_SLEEP_COST_SERIES series at one depth.
 * @param quantisation_nanojoules What the counter cannot tell apart over one series: two of its units.
 * @param timestamp_counter_hertz The clock sleeps are timed on; 0 leaves the per-cycle power at 0.
 * @param out                     Receives the costs, written whole; MEASURED or the reason they are not.
 */
void kernel_sleep_cost_estimate(const KernelSleepCostSeries_t *series, uint64_t quantisation_nanojoules,
                                uint64_t timestamp_counter_hertz, KernelSleepCost_t *out);

/**
 * @brief What a sleep of @p asleep_cycles at a depth of costs @p cost costs, in nanojoules times 2^20.
 * @param cost          MEASURED costs.
 * @param asleep_cycles How long the sleep lasts.
 * @return E * 2^20 + P per 2^20 cycles * cycles, the largest value for a product past 64 bits.
 */
uint64_t kernel_sleep_cost_of(const KernelSleepCost_t *cost, uint64_t asleep_cycles);

/**
 * @brief The depth a sleep should enter: among the allowed candidates whose sleep would last, the one with the least
 *        E + P * T when every allowed candidate is MEASURED, a tie to the shallower; otherwise the deepest whose
 *        residency the sleep outlasts, the shallowest allowed one always qualifying.
 * @param candidates  By depth, shallowest first.
 * @param count       How many.
 * @param out_by_cost Receives whether measured costs decided; may be NULL.
 * @return The depth; KERNEL_SLEEP_COST_NO_DEPTH when no candidate is allowed with a sleep that lasts.
 */
uint32_t kernel_sleep_cost_choose(const KernelSleepCostCandidate_t *candidates, uint32_t count, bool *out_by_cost);

/**
 * @brief How long a sleep from @p now to @p wake lasts, capped by the longest sleep.
 * @param now     The clock now.
 * @param wake    The wake-up, on the same clock; UINT64_MAX when none is planned.
 * @param longest The longest sleep, in the clock's cycles.
 * @return The cycles from @p now to @p wake, at most @p longest; 0 for a wake-up not after @p now.
 */
uint64_t kernel_sleep_cost_cycles_until(uint64_t now, uint64_t wake, uint64_t longest);

/**
 * @brief Replays a session's sleeps: each chosen again among @p depths (kernel_sleep_cost_choose), then taken at the
 *        shallowest and at the deepest allowed depth, and the three costed.
 *
 * @details A sleep at a depth lasts its window less the depth's lead. A fixed depth whose lead outlasts the window
 *          misses its wake-up, and is costed asleep the whole window; with no depth allowed, every sleep misses at
 *          all three, costed nothing.
 *
 * @param records     The sleeps, in any order: KERNEL_SLEEP_COST_RECORDED at most, the latest ones past it.
 * @param count       How many sleeps were recorded; past KERNEL_SLEEP_COST_RECORDED, the ones @p records kept.
 * @param depths      By depth, shallowest first: the session's last word on each.
 * @param depth_count How many, KERNEL_SLEEP_COST_DEPTHS at most: deeper ones are left out.
 * @param out         Receives the costs, the misses and the agreements, written whole.
 */
void kernel_sleep_cost_replay(const KernelSleepCostRecord_t *records, uint32_t count,
                              const KernelSleepCostReplayDepth_t *depths, uint32_t depth_count,
                              KernelSleepCostReplay_t *out);

/**
 * @brief The name the telemetry gives a source.
 * @param source A KernelSleepCostSource_t.
 * @return "not_calibrated", "no_counter", "measured", "noisy", "unseparated", "implausible", "unread" or "untimed";
 *         "unknown" otherwise.
 */
const char *kernel_sleep_cost_source_name(uint32_t source);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_SLEEP_COST_H */
