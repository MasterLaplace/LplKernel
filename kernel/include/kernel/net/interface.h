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
 * @file interface.h
 * @brief An IPv4 interface on one network card: its addresses, its ARP cache, and the poll that answers ARP and ping.
 *
 * kernel_network_interface_poll harvests the card, reads every frame where the card wrote it, answers the ARP
 * requests for the interface's address and the echo requests sent to it, releases the frames and kicks the
 * card once. Every frame dropped is counted by its cause. Time is the caller's, so that a replay of the same
 * frames at the same times answers the same way.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_INTERFACE_H_
#define KERNEL_NET_INTERFACE_H_

#include <kernel/net/arp.h>
#include <kernel/net/device.h>
#include <kernel/net/ipv4.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What went through an interface, every frame dropped counted by its cause.
 */
typedef struct KernelNetworkInterfaceCounters {
    uint32_t frames_received;                      /**< Frames the poll read. */
    uint32_t frames_truncated;                     /**< Shorter than an Ethernet header. */
    uint32_t frames_for_others;                    /**< Addressed to another station. */
    uint32_t frames_of_unknown_type;               /**< Neither ARP nor IPv4. */
    uint32_t address_resolution_malformed;         /**< ARP packets of another size, hardware or protocol. */
    uint32_t address_resolution_requests_answered; /**< Requests for the interface's address, answered. */
    uint32_t address_resolution_replies_learned;   /**< Replies to the interface, learned. */
    uint32_t address_resolution_requests_sent;     /**< Requests the interface sent. */
    uint32_t address_resolution_for_others;        /**< ARP packets about another address. */
    uint32_t internet_protocol_refused[KERNEL_INTERNET_PROTOCOL_STATUSES]; /**< IPv4 packets refused, by status. */
    uint32_t internet_protocol_for_others;                                 /**< IPv4 packets to another address. */
    uint32_t internet_protocol_unhandled; /**< IPv4 packets of a protocol the interface does not handle. */
    uint32_t echo_requests_answered;      /**< Echo requests answered. */
    uint32_t echo_replies_received;       /**< Echo replies received. */
    uint32_t control_messages_ignored;    /**< ICMP messages neither echo request nor reply, or unsound. */
    uint32_t transmit_ring_full;          /**< Answers dropped for want of a transmit slot. */
} KernelNetworkInterfaceCounters_t;

/**
 * @brief An IPv4 interface on one network card.
 */
typedef struct KernelNetworkInterface {
    KernelNetworkDevice_t *device;             /**< The card. */
    uint32_t address;                          /**< The interface's IPv4 address, host order. */
    uint32_t netmask;                          /**< Its netmask, host order. */
    uint32_t gateway;                          /**< The next hop off the subnet, 0 for none. */
    uint16_t next_identification;              /**< The identification of the next IPv4 packet sent. */
    KernelAddressResolutionCache_t cache;      /**< The station addresses learned. */
    KernelNetworkInterfaceCounters_t counters; /**< What went through. */
} KernelNetworkInterface_t;

/**
 * @brief Sets an interface up on @p device, with its addresses and an empty ARP cache.
 *
 * @param interface        The interface.
 * @param device           A ready card, with one queue pair at least.
 * @param address          Its IPv4 address, host order.
 * @param netmask          Its netmask.
 * @param gateway          The next hop off the subnet, 0 for none.
 * @param cache_lifetime   How long a learned station address lasts, in the unit of the times polls take.
 * @return false, @p interface untouched, when @p device is NULL or has no queue.
 */
extern bool kernel_network_interface_initialize(KernelNetworkInterface_t *interface, KernelNetworkDevice_t *device,
                                                uint32_t address, uint32_t netmask, uint32_t gateway,
                                                uint32_t cache_lifetime);

/**
 * @brief Reads every frame the card received, answers what is the interface's to answer, and kicks the card.
 *
 * @param interface The interface.
 * @param now       The caller's time.
 * @return Frames read.
 */
extern uint32_t kernel_network_interface_poll(KernelNetworkInterface_t *interface, uint32_t now);

/**
 * @brief Broadcasts an ARP request for @p target, for a later poll to learn its station address.
 *
 * @param interface The interface.
 * @param target    The IPv4 address asked for, host order.
 * @return false when no transmit slot was free.
 */
extern bool kernel_network_interface_request_address(KernelNetworkInterface_t *interface, uint32_t target);

/**
 * @brief The station address a packet to @p destination goes to: its own on the subnet, the gateway's off it.
 *
 * @param interface   The interface.
 * @param destination The IPv4 address, host order.
 * @param now         The caller's time.
 * @param out_station Receives the station address.
 * @return false, @p out_station untouched, when the next hop is not learned or expired: the caller then asks for
 *         it with kernel_network_interface_request_address, and tries again later.
 */
extern bool kernel_network_interface_find_station(const KernelNetworkInterface_t *interface, uint32_t destination,
                                                  uint32_t now, uint8_t *out_station);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_INTERFACE_H_ */
