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
 * @file spsc_ring.h
 * @brief The indices of a single-producer single-consumer ring; the slots stay with their owner.
 *
 * A driver keeps its slots in an array of its own element type, of a power-of-two length, and a
 * KernelSpscRing_t next to it. The producer asks the ring which slots it may fill, fills them in
 * place and publishes them; the consumer asks which slots are ready, reads them in place and
 * releases them. Nothing is copied twice, any element type fits, and a run of slots is handed out
 * at once, up to the end of the array.
 *
 * Each end owns one control block: its own index, and its last read of the other end's index. The
 * two blocks sit KERNEL_SPSC_RING_INTERFERENCE_SIZE apart, so on two cores an end touches only its
 * own line, and reads the other's only when its copy says the ring is full or empty. Indices are
 * free-running 32-bit counters masked into the slots, so a ring of N slots holds N elements. An
 * index is published with a release store and read with an acquire load, the operations of the
 * kernel C library's stdatomic.h, which also order the slot accesses against an interrupt handler
 * on the same core.
 *
 * The producer is the only writer of KernelSpscRingProducerEnd_t and the consumer of
 * KernelSpscRingConsumerEnd_t. The size, rejected-count and capacity accessors read and may be
 * called from anywhere.
 *
 * lpl::container::RingBuffer in LplPlugin is the same protocol for C++ elements, whose slots it
 * constructs and destroys itself.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-08
 **************************************************************************/

#ifndef KERNEL_MEMORY_SPSC_RING_H_
#define KERNEL_MEMORY_SPSC_RING_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Distance between what the producer writes and what the consumer writes.
 *
 * @details The compiler's figure for the target, __GCC_DESTRUCTIVE_SIZE (256 on ARM64, where some
 *          cores have 256-byte lines, 128 on POWER), and never less than 128: x86's spatial
 *          prefetcher fetches 64-byte lines in 128-byte aligned pairs, and Apple's ARM64 cores have
 *          128-byte lines. Too much distance costs memory only; too little brings back the coherence
 *          traffic it is there to remove.
 */
#if defined(__GCC_DESTRUCTIVE_SIZE) && __GCC_DESTRUCTIVE_SIZE > 128
#    define KERNEL_SPSC_RING_INTERFERENCE_SIZE __GCC_DESTRUCTIVE_SIZE
#else
#    define KERNEL_SPSC_RING_INTERFERENCE_SIZE 128u
#endif

/** @brief Largest capacity: the free-running indices must tell a full ring from an empty one. */
#define KERNEL_SPSC_RING_MAX_CAPACITY 0x80000000u

/**
 * @brief What the producer writes, alone in its interference span.
 */
typedef struct __attribute__((aligned(KERNEL_SPSC_RING_INTERFERENCE_SIZE))) KernelSpscRingProducerEnd {
    uint32_t write_index;       /**< Next slot to fill, published with release. */
    uint32_t cached_read_index; /**< Last read_index the producer read. */
    uint32_t rejected_count;    /**< Elements that did not fit. */
    uint32_t mask;              /**< Capacity minus one, fixed at initialization. */
} KernelSpscRingProducerEnd_t;

/**
 * @brief What the consumer writes, alone in its interference span.
 */
typedef struct __attribute__((aligned(KERNEL_SPSC_RING_INTERFERENCE_SIZE))) KernelSpscRingConsumerEnd {
    uint32_t read_index;         /**< Next slot to read, published with release. */
    uint32_t cached_write_index; /**< Last write_index the consumer read. */
    uint32_t mask;               /**< Capacity minus one, fixed at initialization. */
} KernelSpscRingConsumerEnd_t;

/**
 * @brief The two ends of one ring.
 */
typedef struct KernelSpscRing {
    KernelSpscRingProducerEnd_t producer; /**< Written by the producer only. */
    KernelSpscRingConsumerEnd_t consumer; /**< Written by the consumer only. */
} KernelSpscRing_t;

_Static_assert(sizeof(KernelSpscRingProducerEnd_t) == KERNEL_SPSC_RING_INTERFERENCE_SIZE &&
                   sizeof(KernelSpscRingConsumerEnd_t) == KERNEL_SPSC_RING_INTERFERENCE_SIZE,
               "each end of the ring fills exactly one interference span");

/**
 * @brief Static initializer of an empty ring of @p capacity slots, a power of two.
 */
#define KERNEL_SPSC_RING_INITIALIZER(capacity)                                                                         \
    {                                                                                                                  \
        .producer = {.write_index = 0u, .cached_read_index = 0u, .rejected_count = 0u, .mask = (capacity) - 1u},       \
        .consumer = {.read_index = 0u, .cached_write_index = 0u, .mask = (capacity) - 1u},                             \
    }

/**
 * @brief Empties @p ring and sets its capacity, with no other thread or handler using it.
 *
 * @param ring     The ring.
 * @param capacity Number of slots of its owner's array: a power of two, at most
 *                 KERNEL_SPSC_RING_MAX_CAPACITY.
 * @return false when @p capacity is not such a power of two; the ring is left as it was.
 */
static inline bool kernel_spsc_ring_initialize(KernelSpscRing_t *ring, uint32_t capacity)
{
    if (capacity == 0u || (capacity & (capacity - 1u)) != 0u || capacity > KERNEL_SPSC_RING_MAX_CAPACITY)
        return false;

    ring->producer.write_index = 0u;
    ring->producer.cached_read_index = 0u;
    ring->producer.rejected_count = 0u;
    ring->producer.mask = capacity - 1u;
    ring->consumer.read_index = 0u;
    ring->consumer.cached_write_index = 0u;
    ring->consumer.mask = capacity - 1u;
    return true;
}

/**
 * @brief Producer: the slots it may fill, contiguous from the next one.
 *
 * @details Reads the consumer's index again only when the cached copy shows fewer than @p wanted
 *          free slots. The run stops at the end of the array: what is left after the wrap comes
 *          with the next call.
 *
 * @param ring           The ring.
 * @param wanted         Most slots the producer wants.
 * @param out_first_slot Receives the index, in the owner's array, of the first slot to fill.
 * @return How many slots from *out_first_slot may be filled, then published; 0 when the ring is full.
 */
static inline uint32_t kernel_spsc_ring_reserve(KernelSpscRing_t *ring, uint32_t wanted, uint32_t *out_first_slot)
{
    KernelSpscRingProducerEnd_t *const producer = &ring->producer;
    const uint32_t capacity = producer->mask + 1u;
    const uint32_t write_index = producer->write_index;
    const uint32_t first_slot = write_index & producer->mask;
    const uint32_t until_wrap = capacity - first_slot;
    uint32_t free_slots = capacity - (write_index - producer->cached_read_index);

    if (free_slots < wanted)
    {
        producer->cached_read_index = atomic_load_acquire(&ring->consumer.read_index);
        free_slots = capacity - (write_index - producer->cached_read_index);
    }
    *out_first_slot = first_slot;
    free_slots = free_slots < wanted ? free_slots : wanted;
    return free_slots < until_wrap ? free_slots : until_wrap;
}

/**
 * @brief Producer: hands @p count filled slots to the consumer, in the order they were reserved.
 */
static inline void kernel_spsc_ring_publish(KernelSpscRing_t *ring, uint32_t count)
{
    atomic_store_release(&ring->producer.write_index, ring->producer.write_index + count);
}

/**
 * @brief Producer: counts @p count elements that did not fit and were dropped.
 */
static inline void kernel_spsc_ring_count_rejected(KernelSpscRing_t *ring, uint32_t count)
{
    atomic_store_relaxed(&ring->producer.rejected_count, ring->producer.rejected_count + count);
}

/**
 * @brief Consumer: the slots ready to read, contiguous from the oldest.
 *
 * @details Reads the producer's index again only when the cached copy shows fewer than @p wanted
 *          ready slots. The run stops at the end of the array: what is left after the wrap comes
 *          with the next call.
 *
 * @param ring           The ring.
 * @param wanted         Most slots the consumer wants.
 * @param out_first_slot Receives the index, in the owner's array, of the oldest ready slot.
 * @return How many slots from *out_first_slot may be read, then released; 0 when the ring is empty.
 */
static inline uint32_t kernel_spsc_ring_peek(KernelSpscRing_t *ring, uint32_t wanted, uint32_t *out_first_slot)
{
    KernelSpscRingConsumerEnd_t *const consumer = &ring->consumer;
    const uint32_t read_index = consumer->read_index;
    const uint32_t first_slot = read_index & consumer->mask;
    const uint32_t until_wrap = consumer->mask + 1u - first_slot;
    uint32_t ready_slots = consumer->cached_write_index - read_index;

    if (ready_slots < wanted)
    {
        consumer->cached_write_index = atomic_load_acquire(&ring->producer.write_index);
        ready_slots = consumer->cached_write_index - read_index;
    }
    *out_first_slot = first_slot;
    ready_slots = ready_slots < wanted ? ready_slots : wanted;
    return ready_slots < until_wrap ? ready_slots : until_wrap;
}

/**
 * @brief Consumer: how many elements are ready, across the wrap, reading the producer's index again.
 */
static inline uint32_t kernel_spsc_ring_ready_count(KernelSpscRing_t *ring)
{
    KernelSpscRingConsumerEnd_t *const consumer = &ring->consumer;

    consumer->cached_write_index = atomic_load_acquire(&ring->producer.write_index);
    return consumer->cached_write_index - consumer->read_index;
}

/**
 * @brief Consumer: the slot of the element @p offset places after the oldest.
 *
 * @param ring   The ring.
 * @param offset Position from the oldest ready element; it must be below what
 *               kernel_spsc_ring_ready_count last returned.
 * @return The index of that slot in the owner's array.
 */
static inline uint32_t kernel_spsc_ring_slot_at(const KernelSpscRing_t *ring, uint32_t offset)
{
    return (ring->consumer.read_index + offset) & ring->consumer.mask;
}

/**
 * @brief Consumer: gives @p count read slots back to the producer, oldest first.
 */
static inline void kernel_spsc_ring_release(KernelSpscRing_t *ring, uint32_t count)
{
    atomic_store_release(&ring->consumer.read_index, ring->consumer.read_index + count);
}

/**
 * @brief Elements in the ring, as one snapshot, from anywhere; stale as soon as it returns.
 */
static inline uint32_t kernel_spsc_ring_size(const KernelSpscRing_t *ring)
{
    const uint32_t read_index = atomic_load_acquire(&ring->consumer.read_index);
    const uint32_t write_index = atomic_load_acquire(&ring->producer.write_index);
    const uint32_t used = write_index - read_index;
    const uint32_t capacity = ring->producer.mask + 1u;

    return used < capacity ? used : capacity;
}

/**
 * @brief Elements the producer could not fit since the ring was initialized.
 */
static inline uint32_t kernel_spsc_ring_rejected_count(const KernelSpscRing_t *ring)
{
    return atomic_load_relaxed(&ring->producer.rejected_count);
}

/**
 * @brief Number of slots, all usable.
 */
static inline uint32_t kernel_spsc_ring_capacity(const KernelSpscRing_t *ring) { return ring->producer.mask + 1u; }

/**
 * @brief The producer's index, for a consumer that sleeps until it changes (processor_sleep_until_write).
 */
static inline const volatile uint32_t *kernel_spsc_ring_write_index_address(const KernelSpscRing_t *ring)
{
    return &ring->producer.write_index;
}

#ifdef __cplusplus
}
#endif

#endif /* !KERNEL_MEMORY_SPSC_RING_H_ */
