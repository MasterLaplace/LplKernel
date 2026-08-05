/**
 * @file tensor_arena.h
 * @brief One large, bounded region for weights and activations.
 *
 * Claimed once at boot, never grown. The kernel heap is a few megabytes and a
 * model is not; treating the arena as a first-class region rather than a very large
 * malloc is what keeps the two from destroying each other.
 *
 * The region is handed to the engine side as a bare pointer and a length, which is
 * the whole seam: `lpl::infer::TensorArena` has a constructor that adopts memory it
 * does not own, so ring 0 and a host run the SAME bump allocator over blocks of
 * different provenance. That matters because the arena's byte accounting is folded
 * by the parity gate — two implementations would be two answers to how many bytes a
 * given sequence of claims costs.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_AI_TENSOR_ARENA_H
#define KERNEL_AI_TENSOR_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Claims the region, once.
 *
 * A second call with the same size is a no-op and reports success; a second call
 * asking for MORE fails rather than reallocating. Growing would mean freeing a block
 * the demon may already hold pointers into, and "never grown" is the property the
 * whole arrangement rests on.
 *
 * @param bytes Region size.
 * @return true when the region exists and is at least @p bytes long.
 */
bool kernel_tensor_arena_initialize(size_t bytes);

/**
 * @brief Releases the region.
 *
 * Only meaningful at shutdown or between smoke passes. Anything holding a pointer
 * into the arena is dangling afterwards, which is why nothing calls this mid-run.
 */
void kernel_tensor_arena_release(void);

/**
 * @brief Base of the region.
 * @return The pointer, or NULL when it has not been claimed.
 */
void *kernel_tensor_arena_base(void);

/**
 * @brief Size of the region.
 * @return The byte count, or 0 when it has not been claimed.
 */
size_t kernel_tensor_arena_size(void);

/**
 * @brief Has the region been claimed?
 * @return true when it exists.
 */
bool kernel_tensor_arena_ready(void);

/**
 * @brief Records how much of the region a run actually used.
 *
 * Reported by the consumer rather than tracked here, because the bump pointer lives
 * on the engine side. Kept as a high-water mark: the useful question at shutdown is
 * "was the region big enough", and the answer is the largest occupancy ever seen,
 * not the last one.
 *
 * @param bytes Bytes the run consumed.
 */
void kernel_tensor_arena_record_used(size_t bytes);

/**
 * @brief The largest occupancy recorded.
 * @return The high-water mark in bytes.
 */
size_t kernel_tensor_arena_high_water(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_AI_TENSOR_ARENA_H */
