/**
 * @file backpressure.h
 * @brief One place that names every bounded queue in the kernel, and what a loss costs.
 *
 * The kernel has four single-producer rings and every one of them counts what it
 * drops when full. Nothing enumerated them, so nothing could notice that a queue had
 * STARTED dropping: the counters existed, and only a human reading the right log line
 * would ever look at one.
 *
 * Blocking the producer is not available here — the writer is often an interrupt
 * handler, which cannot wait — and overwriting the oldest entry would corrupt a
 * message a consumer is halfway through reading. So every ring refuses the newcomer.
 * What differs between them is not the mechanism but the CONSEQUENCE, and that is what
 * this file makes explicit:
 *
 *   - a dropped scan code costs one keystroke, and nothing else;
 *   - a dropped byte in the middle of a sentence costs the sentence.
 *
 * Declaring which of the two a queue is turns "it drops when full" from a comment into
 * a fact something can check. The reconciler notes a loss on a tolerant queue and
 * treats a loss on an intolerant one as drift.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_MEMORY_BACKPRESSURE_H
#define KERNEL_MEMORY_BACKPRESSURE_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/drivers/serial.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bounded queues the registry can hold. Registration past this is refused, not ignored. */
#define KERNEL_BACKPRESSURE_MAX_QUEUES 8u

/**
 * @brief What is lost when a full queue refuses an item.
 */
typedef enum
{
    /** The item, and only the item. A missed keystroke is a missed keystroke. */
    KERNEL_BACKPRESSURE_POLICY_DROP_TOLERATED = 0,

    /** The item and the meaning of its neighbours — a sentence read half-through. */
    KERNEL_BACKPRESSURE_POLICY_DROP_CORRUPTS = 1,
} KernelBackpressurePolicy_t;

/** Reads a queue's own drop counter. The registry never owns the count, only finds it. */
typedef uint32_t (*KernelBackpressureDropCountFunction_t)(void);

/**
 * @brief A bounded queue as declared to the registry.
 */
typedef struct {
    const char *name;                                 /**< Queue name, used as a telemetry field. */
    KernelBackpressurePolicy_t policy;                /**< What a loss costs. */
    uint32_t capacity_items;                          /**< Items the ring holds before refusing. */
    KernelBackpressureDropCountFunction_t drop_count; /**< The queue's own counter. */
} KernelBackpressureQueue_t;

/**
 * @brief Forget every registered queue.
 */
void kernel_backpressure_reset(void);

/**
 * @brief Declare a bounded queue and how it behaves under pressure.
 *
 * @param name Queue name; must carry neither a space nor an '=' so it can be a
 *             telemetry field name.
 * @param policy What a dropped item costs.
 * @param capacity_items Items the ring holds.
 * @param drop_count The queue's own drop counter, never null.
 * @return true when the queue was registered, false when the table is full or an
 *         argument is missing.
 */
bool kernel_backpressure_register(const char *name, KernelBackpressurePolicy_t policy, uint32_t capacity_items,
                                  KernelBackpressureDropCountFunction_t drop_count);

/**
 * @brief Number of registered queues.
 *
 * @return The count.
 */
uint32_t kernel_backpressure_get_queue_count(void);

/**
 * @brief Access a registered queue by index.
 *
 * @param index Index below kernel_backpressure_get_queue_count().
 * @return The queue, or NULL when the index is out of range.
 */
const KernelBackpressureQueue_t *kernel_backpressure_get_queue(uint32_t index);

/**
 * @brief Items dropped across every registered queue.
 *
 * @return The sum of all drop counters.
 */
uint32_t kernel_backpressure_get_total_drop_count(void);

/**
 * @brief Items dropped on queues where a loss corrupts what remains.
 *
 * @details The number that matters. A tolerant queue dropping under load is the
 *          design working; an intolerant one dropping means a message was damaged.
 *
 * @return The sum over KERNEL_BACKPRESSURE_POLICY_DROP_CORRUPTS queues.
 */
uint32_t kernel_backpressure_get_intolerant_drop_count(void);

/**
 * @brief Emit one telemetry record per registered queue.
 *
 * @param serial Output port.
 */
void kernel_backpressure_report(Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_MEMORY_BACKPRESSURE_H */
