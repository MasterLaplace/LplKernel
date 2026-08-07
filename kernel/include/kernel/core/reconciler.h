/**
 * @file reconciler.h
 * @brief Compares what the kernel declared to what it is doing, every frame.
 *
 * The kernel already states what it intends: an arena capacity, a tolerance for
 * unbounded allocation inside a tick, a count of pages that must stay read-only, a
 * set of queues and what a loss on each one costs. And it already measures all of
 * those. What it never did was put the two side by side while running.
 *
 * Every invariant check in this tree is either a boot smoke — asked once, on the way
 * up — or a parity gate, a signature compared at the end. Neither notices a system
 * that was correct at boot and stopped being correct at frame nine thousand. That is
 * drift, and drift is what a declarative system is supposed to be immune to: the
 * whole claim of an immutable node is that what runs equals what was declared, for as
 * long as it runs.
 *
 * So this holds no new measurement. Every number it reads was already there and
 * already printed somewhere. It turns them into a contract that is re-checked, and
 * counts the passes where reality stopped matching.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_CORE_RECONCILER_H
#define KERNEL_CORE_RECONCILER_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/drivers/serial.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Timer ticks between two comparison passes.
 *
 * The kernel's periodic tick drives the reconciler, because it runs on every
 * profile and does not depend on the engine having enabled anything. Sampling
 * rather than checking on each tick keeps the cost off a 1 kHz path for a
 * resolution nobody needs: at 100 Hz this is a pass every 640 ms, and drift that
 * lasts less than that is still caught on the next pass if it persists — the mask
 * is sticky, so nothing that lasts is ever missed.
 */
#define KERNEL_RECONCILER_TICK_SAMPLE_PERIOD 64u

/**
 * @brief Idle iterations kernel_reconciler_wait_for_periodic_pass() will spend.
 *
 * Each iteration halts until the next interrupt, so this is counted in interrupts
 * and not in cycles. Four times the sample period leaves room for a tick or two to
 * be missed without turning a bounded wait into a failed one.
 */
#define KERNEL_RECONCILER_PERIODIC_WAIT_LIMIT (4u * KERNEL_RECONCILER_TICK_SAMPLE_PERIOD)

/** Invariants compared on each pass. Each is one bit of the drift mask. */
typedef enum
{
    /** The code and constant pages are still protected. */
    KERNEL_RECONCILER_INVARIANT_SECTION_PROTECTION = 0,

    /** The processor still enforces read-only pages against ring 0. */
    KERNEL_RECONCILER_INVARIANT_WRITE_PROTECT = 1,

    /** As many pages are read-only as when the declaration was made. */
    KERNEL_RECONCILER_INVARIANT_READ_ONLY_PAGE_COUNT = 2,

    /** The frame arena has not been pushed past the capacity it declared. */
    KERNEL_RECONCILER_INVARIANT_FRAME_ARENA = 3,

    /** Unbounded allocations inside the tick stay within the declared budget. */
    KERNEL_RECONCILER_INVARIANT_REAL_TIME = 4,

    /** No queue whose losses corrupt meaning has lost anything. */
    KERNEL_RECONCILER_INVARIANT_QUEUE_INTEGRITY = 5,

    /** Count, not an invariant. */
    KERNEL_RECONCILER_INVARIANT_COUNT = 6,
} KernelReconcilerInvariant_t;

/**
 * @brief What the kernel says it will do.
 */
typedef struct {
    uint32_t frame_arena_capacity_bytes;   /**< Ceiling on frame arena usage. */
    uint32_t real_time_violation_budget;   /**< Unbounded allocations tolerated in a tick. */
    uint32_t read_only_page_count;         /**< Pages the section protection must keep. */
    bool require_section_protection;       /**< Whether the protection must stay active. */
    bool require_write_protect;            /**< Whether CR0.WP must stay set. */
} KernelReconcilerDeclaration_t;

/**
 * @brief Adopt a declaration and clear the drift history.
 *
 * @param declaration What the kernel commits to; copied, not retained by pointer.
 */
void kernel_reconciler_declare(const KernelReconcilerDeclaration_t *declaration);

/**
 * @brief Report whether a declaration has been adopted.
 *
 * @return true once kernel_reconciler_declare() has run.
 */
bool kernel_reconciler_is_declared(void);

/**
 * @brief Run one comparison pass.
 *
 * Cheap enough for the end of every fixed step: a handful of reads of counters
 * that are already maintained, and no allocation.
 *
 * @return The number of invariants in drift on this pass, zero when reality still
 *         matches the declaration.
 */
uint32_t kernel_reconciler_check(void);

/**
 * @brief Run one pass on behalf of the periodic tick.
 *
 * Separate from kernel_reconciler_check() for one reason: it counts. "Passes have
 * happened" is satisfied by the smoke driving them by hand, which says nothing
 * about whether the continuous check is actually running — so the two drivers are
 * counted apart and only this one answers that question.
 */
void kernel_reconciler_check_periodic(void);

/**
 * @brief Passes taken by the periodic tick since boot.
 *
 * @return The count. Never reset by kernel_reconciler_declare(), because it is a
 *         fact about the driver rather than about the current contract.
 */
uint32_t kernel_reconciler_get_periodic_pass_count(void);

/**
 * @brief Wait, with a bound, for the periodic driver to take its first pass.
 *
 * Used by the report so the liveness it prints is a fact rather than a race with
 * how fast the machine booted. Bounded, and it idles rather than spinning: a dead
 * timer must make this return, not hang.
 *
 * @return true if a periodic pass has happened by the time it returns.
 */
bool kernel_reconciler_wait_for_periodic_pass(void);

/**
 * @brief Passes run since the declaration was adopted.
 *
 * @return The count. A drift count of zero means nothing if this is zero too.
 */
uint32_t kernel_reconciler_get_pass_count(void);

/**
 * @brief Passes that found at least one invariant in drift.
 *
 * @return The count.
 */
uint32_t kernel_reconciler_get_drift_count(void);

/**
 * @brief Which invariants have drifted at any point since the declaration.
 *
 * @details Sticky, one bit per KernelReconcilerInvariant_t: a drift that lasts a
 *          single frame is exactly the kind that a snapshot at the end would miss.
 *
 * @return The accumulated mask.
 */
uint32_t kernel_reconciler_get_drift_mask(void);

/**
 * @brief Name of an invariant, for reporting.
 *
 * @param invariant Which one.
 * @return Its name, or "unknown" when the value is out of range.
 */
const char *kernel_reconciler_get_invariant_name(KernelReconcilerInvariant_t invariant);

/**
 * @brief Emit the reconciler's state as one telemetry record.
 *
 * Called by whoever is in thread context and wants the picture, never by the pass
 * itself: COM1 runs at 9600 baud, so writing a record costs about a millisecond
 * per character. A report from inside the timer interrupt would hold the handler
 * for longer than the period it fires on.
 *
 * @param serial Output port.
 */
void kernel_reconciler_report(Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_CORE_RECONCILER_H */
