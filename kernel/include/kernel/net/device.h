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
 * @file device.h
 * @brief A network card as the stack sees it, whatever its model: queues of kernel_spsc_ring over its slots.
 *
 * Every card driver fills one KernelNetworkDevice_t. Each of its queues is a receive and a transmit
 * kernel_spsc_ring whose slots are the card's own descriptors, each with a buffer of buffer_bytes:
 *
 * - the stack consumes the receive ring with kernel_spsc_ring_peek and kernel_spsc_ring_release, reading a
 *   slot's frame and length through kernel_network_receive_bytes and kernel_network_receive_length;
 * - the stack produces on the transmit ring with kernel_spsc_ring_reserve and kernel_spsc_ring_publish,
 *   writing a frame into kernel_network_transmit_buffer and describing it with kernel_network_transmit_describe.
 *
 * Per frame, the stack touches nothing but generic fields: a buffer at a base plus the slot times a stride,
 * a length at a stride in the card's own receive descriptors, and one word at a stride in its transmit
 * descriptors, written as a length or'ed with the card's command template. Per batch, the card's own
 * operations harvest the frames it received and kick the frames published: one indirect call a batch, never
 * one a frame. Its link is read on a caller's demand, never per batch: on most cards it is a register read,
 * which costs a VM exit under virtualisation.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_DEVICE_H_
#define KERNEL_NET_DEVICE_H_

#include <kernel/memory/spsc_ring.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KERNEL_NETWORK_ADDRESS_BYTES 6u /**< Bytes of a station (MAC) address. */

/**
 * @brief One queue pair of a network card.
 */
typedef struct KernelNetworkQueue {
    KernelSpscRing_t *receive;                /**< Frames received: the card's harvest produces, the stack consumes. */
    const uint8_t *receive_buffers;           /**< Slot s's frame starts at receive_buffers + s * buffer_bytes. */
    const volatile uint16_t *receive_lengths; /**< Slot s's length is receive_lengths[s * receive_length_stride]. */
    uint32_t receive_length_stride;           /**< uint16_t between two slots' lengths. */
    KernelSpscRing_t *transmit;               /**< Frames to send: the stack produces, the card's kick consumes. */
    uint8_t *transmit_buffers;                /**< Slot s's buffer starts at transmit_buffers + s * buffer_bytes. */
    volatile uint32_t *transmit_words; /**< Slot s's descriptor word is transmit_words[s * transmit_word_stride]. */
    uint32_t transmit_word_stride;     /**< uint32_t between two slots' descriptor words. */
    uint32_t transmit_command;         /**< Or'ed with a frame's length into its descriptor word. */
} KernelNetworkQueue_t;

struct KernelNetworkDevice;

/**
 * @brief What a card does once a batch, on the stack's call.
 */
typedef struct KernelNetworkDeviceOperations {
    uint32_t (*harvest)(struct KernelNetworkDevice *device, uint32_t queue); /**< Publishes the frames received. */
    uint32_t (*kick)(struct KernelNetworkDevice *device, uint32_t queue);    /**< Hands the frames published over. */
    bool (*link_up)(struct KernelNetworkDevice *device);                     /**< Reads whether its link is up. */
} KernelNetworkDeviceOperations_t;

/**
 * @brief A network card, whatever its model.
 */
typedef struct KernelNetworkDevice {
    const char *name;                                  /**< The driver and model, for the logs. */
    uint8_t address[KERNEL_NETWORK_ADDRESS_BYTES];     /**< Its station address. */
    uint32_t buffer_bytes;                             /**< Bytes of each slot's buffer. */
    uint32_t frame_least_bytes;                        /**< Shortest frame a transmit describe takes. */
    uint32_t frame_most_bytes;                         /**< Longest, its CRC left to the card. */
    uint32_t queue_count;                              /**< Queue pairs in queues. */
    KernelNetworkQueue_t *queues;                      /**< Its queue pairs. */
    const KernelNetworkDeviceOperations_t *operations; /**< What it does once a batch. */
} KernelNetworkDevice_t;

/**
 * @brief The first byte of the frame in receive slot @p slot of @p queue, where the card wrote it.
 *
 * @param device The card.
 * @param queue  Its queue pair.
 * @param slot   A slot kernel_spsc_ring_peek handed out.
 * @return The frame's first byte, valid until the slot is released.
 */
static inline const uint8_t *kernel_network_receive_bytes(const KernelNetworkDevice_t *device,
                                                          const KernelNetworkQueue_t *queue, uint32_t slot);

/**
 * @brief The bytes of the frame in receive slot @p slot of @p queue, its CRC stripped.
 *
 * @param queue The queue pair.
 * @param slot  A slot kernel_spsc_ring_peek handed out.
 * @return Its length.
 */
static inline uint16_t kernel_network_receive_length(const KernelNetworkQueue_t *queue, uint32_t slot);

/**
 * @brief The buffer of transmit slot @p slot of @p queue, to write a frame into.
 *
 * @param device The card.
 * @param queue  Its queue pair.
 * @param slot   A slot kernel_spsc_ring_reserve handed out.
 * @return Its first byte; device->buffer_bytes follow.
 */
static inline uint8_t *kernel_network_transmit_buffer(const KernelNetworkDevice_t *device,
                                                      const KernelNetworkQueue_t *queue, uint32_t slot);

/**
 * @brief Describes the frame of @p length bytes written into transmit slot @p slot, for kernel_spsc_ring_publish.
 *
 * @param device The card.
 * @param queue  Its queue pair.
 * @param slot   A slot kernel_spsc_ring_reserve handed out.
 * @param length The frame's bytes, from device->frame_least_bytes to device->frame_most_bytes.
 * @return false, with the slot left as it was, for a length out of range: such a slot is not to be published.
 */
static inline bool kernel_network_transmit_describe(const KernelNetworkDevice_t *device,
                                                    const KernelNetworkQueue_t *queue, uint32_t slot, uint32_t length);

#include <kernel/net/device.inl>

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_DEVICE_H_ */
