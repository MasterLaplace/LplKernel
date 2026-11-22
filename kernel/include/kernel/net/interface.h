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
 * card once. Every frame read lands in exactly one fate, so that the fates and the IPv4 refusals sum to the
 * frames received. Time is the caller's, so that a replay of the same frames at the same times answers the
 * same way.
 *
 * The card's limits and lengths are checked as an untrusted neighbour's: the interface refuses to set up on
 * bounds out of order, and refuses a received length longer than a buffer. The link is not read per batch: a
 * register read costs a VM exit under virtualisation, and a frame sent while the link is down waits in the
 * ring as it would anyway.
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
 * @brief What became of a frame received, but the IPv4 packets its parse refused, counted by their status.
 */
typedef enum KernelNetworkInterfaceFate {
    KERNEL_NETWORK_INTERFACE_FRAME_OVERSIZED = 0,   /**< Longer than the card's buffers: a length not believed. */
    KERNEL_NETWORK_INTERFACE_FRAME_TRUNCATED,       /**< Shorter than an Ethernet header. */
    KERNEL_NETWORK_INTERFACE_FRAME_FOR_OTHERS,      /**< To another station, or to a group but the broadcast. */
    KERNEL_NETWORK_INTERFACE_FRAME_FROM_GROUP,      /**< From a group address, which no station sends from. */
    KERNEL_NETWORK_INTERFACE_FRAME_OF_UNKNOWN_TYPE, /**< Neither ARP nor IPv4. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_MALFORMED,         /**< ARP of another size, hardware or protocol. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_OPERATION_UNKNOWN, /**< ARP neither request nor reply. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_FROM_GROUP,        /**< ARP whose sender station is a group. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_FOR_OTHERS,        /**< About another address, teaching nothing. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_MERGED,            /**< About another, refreshing a station held. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REQUEST_ANSWERED,  /**< A request for the interface, answered. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REPLY_LEARNED,     /**< A reply that filled or refreshed an entry. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_REPLY_UNSOLICITED, /**< A reply about an address not held. */
    KERNEL_NETWORK_INTERFACE_ADDRESS_RESOLUTION_SENDER_REFUSED,    /**< A reply from a sender no entry may hold. */
    KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_FOR_OTHERS,         /**< IPv4 to another address. */
    KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_LINK_BROADCAST,     /**< To the interface, in a frame to a group. */
    KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_SOURCE_REFUSED,     /**< From a source no host sends from. */
    KERNEL_NETWORK_INTERFACE_INTERNET_PROTOCOL_UNHANDLED,          /**< Of a protocol the interface does not take. */
    KERNEL_NETWORK_INTERFACE_CONTROL_MESSAGE_MALFORMED,            /**< ICMP too short, or its checksum fails. */
    KERNEL_NETWORK_INTERFACE_CONTROL_MESSAGE_IGNORED,              /**< ICMP neither echo request nor reply. */
    KERNEL_NETWORK_INTERFACE_ECHO_ANSWERED,                        /**< Echo requests answered. */
    KERNEL_NETWORK_INTERFACE_ECHO_TOO_LONG,                        /**< Echo requests whose reply fits no frame. */
    KERNEL_NETWORK_INTERFACE_ECHO_REPLY_RECEIVED,                  /**< Echo replies. */
    KERNEL_NETWORK_INTERFACE_ANSWER_RING_FULL,                     /**< Answers dropped for want of a slot. */
    KERNEL_NETWORK_INTERFACE_ANSWER_REFUSED,                       /**< Answers the card's describe refused. */
    KERNEL_NETWORK_INTERFACE_FATES                                 /**< How many fates there are. */
} KernelNetworkInterfaceFate_t;

/**
 * @brief What went through an interface.
 */
typedef struct KernelNetworkInterfaceCounters {
    uint32_t frames_received;                                              /**< Frames the poll read. */
    uint32_t fates[KERNEL_NETWORK_INTERFACE_FATES];                        /**< Frames read, by their fate. */
    uint32_t internet_protocol_refused[KERNEL_INTERNET_PROTOCOL_STATUSES]; /**< IPv4 packets refused, by status. */
    uint32_t address_resolution_requests_sent;                             /**< Requests the interface sent. */
    uint32_t address_resolution_requests_unsent;                           /**< Requests it could not publish. */
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
    bool transmit_exhausted;                   /**< Whether a kick this batch freed no slot: it sends no more. */
    KernelAddressResolutionCache_t cache;      /**< The station addresses learned. */
    KernelNetworkInterfaceCounters_t counters; /**< What went through. */
} KernelNetworkInterface_t;

/**
 * @brief Whether an interface set up, or why not.
 */
typedef enum KernelNetworkInterfaceSetup {
    KERNEL_NETWORK_INTERFACE_READY = 0,        /**< Set up. */
    KERNEL_NETWORK_INTERFACE_NO_DEVICE,        /**< No card. */
    KERNEL_NETWORK_INTERFACE_NO_QUEUE,         /**< No queue pair, or one missing a ring or an array. */
    KERNEL_NETWORK_INTERFACE_NO_OPERATIONS,    /**< One of the card's operations missing. */
    KERNEL_NETWORK_INTERFACE_BAD_FRAME_BOUNDS, /**< Not frame_least <= frame_most <= buffer_bytes, 60 <= frame_most. */
} KernelNetworkInterfaceSetup_t;

/**
 * @brief Whether an ARP request for a destination's next hop went out, or why not.
 */
typedef enum KernelNetworkInterfaceRequest {
    KERNEL_NETWORK_INTERFACE_REQUEST_SENT = 0,    /**< Published, for the next poll's kick to send. */
    KERNEL_NETWORK_INTERFACE_REQUEST_NEEDLESS,    /**< A broadcast or multicast, which maps to a station without ARP. */
    KERNEL_NETWORK_INTERFACE_REQUEST_UNREACHABLE, /**< An address no packet leaves for: 0/8, 127/8, 240/4. */
    KERNEL_NETWORK_INTERFACE_REQUEST_RING_FULL,   /**< No transmit slot was free. */
    KERNEL_NETWORK_INTERFACE_REQUEST_REFUSED,     /**< The card's describe refused the request's length. */
} KernelNetworkInterfaceRequest_t;

/**
 * @brief Sets an interface up on @p device, with its addresses and an empty ARP cache.
 *
 * @param interface      The interface.
 * @param device         A ready card.
 * @param address        Its IPv4 address, host order.
 * @param netmask        Its netmask.
 * @param gateway        The next hop off the subnet, 0 for none.
 * @param cache_lifetime How long a learned station address lasts, in the unit of the times polls take.
 * @return KERNEL_NETWORK_INTERFACE_READY, or why @p device is refused, @p interface then untouched.
 */
extern KernelNetworkInterfaceSetup_t kernel_network_interface_initialize(KernelNetworkInterface_t *interface,
                                                                         KernelNetworkDevice_t *device,
                                                                         uint32_t address, uint32_t netmask,
                                                                         uint32_t gateway, uint32_t cache_lifetime);

/**
 * @brief Reads every frame the card received, answers what is the interface's to answer, and kicks the card.
 *
 * @details It also frees the cache's expired entries, so the caller's time must not advance by 2^32 or more
 *          between two polls.
 *
 * @param interface The interface.
 * @param now       The caller's time.
 * @return Frames read.
 */
extern uint32_t kernel_network_interface_poll(KernelNetworkInterface_t *interface, uint32_t now);

/**
 * @brief Broadcasts an ARP request for the next hop of @p destination, which becomes wanted in the cache.
 *
 * @details The request leaves with the next poll's kick: one kick a batch.
 *
 * @param interface   The interface.
 * @param destination The IPv4 address a packet is to go to, host order.
 * @param now         The caller's time.
 * @return KERNEL_NETWORK_INTERFACE_REQUEST_SENT, or why no request was published.
 */
extern KernelNetworkInterfaceRequest_t kernel_network_interface_request_address(KernelNetworkInterface_t *interface,
                                                                                uint32_t destination, uint32_t now);

/**
 * @brief The station address a packet to @p destination goes to.
 *
 * @details The broadcast station for the limited broadcast and the subnet's, the station RFC 1112 (6.4) maps a
 *          multicast group to, and otherwise the next hop's from the cache: the destination's own on the
 *          subnet, the gateway's off it.
 *
 * @param interface   The interface.
 * @param destination The IPv4 address, host order.
 * @param now         The caller's time.
 * @param out_station Receives the station address.
 * @return false, @p out_station untouched, when the next hop is not learned or expired, the caller then asking for
 *         it with kernel_network_interface_request_address, or when no packet leaves for @p destination.
 */
extern bool kernel_network_interface_find_station(const KernelNetworkInterface_t *interface, uint32_t destination,
                                                  uint32_t now, uint8_t *out_station);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_INTERFACE_H_ */
