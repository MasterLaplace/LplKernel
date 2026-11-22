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
 * @brief An IPv4 interface on one network card: its addresses, its ARP cache, its UDP ports, and the poll that
 *        answers ARP and ping and delivers datagrams.
 *
 * kernel_network_interface_poll harvests the card, reads every frame where the card wrote it, answers the ARP
 * requests for the interface's address and the echo requests sent to it, delivers each datagram to its port,
 * releases the frames and kicks the card once.
 *
 * A port bound lent gets its datagrams where the card wrote them: one consumer, the engine's tick, reads every
 * lent datagram in their order of arrival and releases them in that order, and the card's slot comes back to it
 * then, the frames after it held until their turn. A port bound copied gets each datagram copied into a ring of
 * its own, for a consumer that drains on its own, such as DHCP or DNS. The poll and the consumer of lent
 * datagrams run on one core.
 *
 * A datagram is sent in place: the caller reserves a transmit buffer, writes its payload where the headers end,
 * and gives the payload's sum, which its serializer adds up as it writes; the interface adds the pseudo-header and
 * the header and folds once. A payload sent to many destinations is summed once.
 *
 * The common datagram, unicast to the interface's station and address, IPv4 without options or fragments from a
 * source it accepts, UDP to a lent port, has a path of its own: each field is read once, as the card wrote it,
 * against values the interface computed in the same byte order at set-up and binding, and the IPv4 header is
 * summed as five loads. Any other frame, or any field that does not match, takes the general path, which refuses
 * it by its cause. A destination whose station is
 * not known is refused by name and asked for by ARP, at most once a request interval: no hidden queue. Every frame read
 *lands in exactly one fate, so that the fates and the IPv4 refusals sum to the frames received. Time is the caller's,
 *so that a replay of the same frames at the same times answers the same way.
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

#include <kernel/memory/spsc_ring.h>
#include <kernel/net/arp.h>
#include <kernel/net/device.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/udp.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Interface sizes
 * @{
 */
#define KERNEL_NETWORK_INTERFACE_PORTS          8u  /**< UDP ports an interface binds. */
#define KERNEL_NETWORK_INTERFACE_LENT_DATAGRAMS 64u /**< Datagrams an interface lends at once: a receive ring's. */
#define KERNEL_NETWORK_INTERFACE_HEADERS_BYTES  42u /**< Ethernet, IPv4 and UDP headers, before a payload sent. */
#define KERNEL_NETWORK_INTERFACE_STATION_HINTS  8u  /**< Hints of a next hop's cache entry, by its address's low bits. \
                                                     */
/** @} */

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
    KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_MALFORMED,              /**< UDP too short, or its length past it. */
    KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_BAD_CHECKSUM,           /**< UDP whose checksum does not hold. */
    KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_CHECKSUM_ABSENT,        /**< UDP without one, to a port that wants it. */
    KERNEL_NETWORK_INTERFACE_USER_DATAGRAM_PORT_UNBOUND,           /**< UDP to a port nobody bound. */
    KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT,                        /**< Datagrams lent where they arrived. */
    KERNEL_NETWORK_INTERFACE_DATAGRAM_LENT_FULL,                   /**< Dropped, every lent place taken. */
    KERNEL_NETWORK_INTERFACE_DATAGRAM_COPIED,                      /**< Datagrams copied into their port's ring. */
    KERNEL_NETWORK_INTERFACE_DATAGRAM_PORT_FULL,                   /**< Dropped, their port's ring full. */
    KERNEL_NETWORK_INTERFACE_DATAGRAM_TOO_LONG_FOR_PORT,           /**< Dropped, longer than a port's buffer. */
    KERNEL_NETWORK_INTERFACE_ANSWER_RING_FULL,                     /**< Answers dropped for want of a slot. */
    KERNEL_NETWORK_INTERFACE_ANSWER_REFUSED,                       /**< Answers the card's describe refused. */
    KERNEL_NETWORK_INTERFACE_FATES                                 /**< How many fates there are. */
} KernelNetworkInterfaceFate_t;

/**
 * @brief What became of a datagram sent.
 */
typedef enum KernelNetworkInterfaceSend {
    KERNEL_NETWORK_INTERFACE_SEND_SENT = 0,              /**< Published, for the next kick to send. */
    KERNEL_NETWORK_INTERFACE_SEND_NO_STATION,            /**< Its next hop's station unknown: asked by ARP. */
    KERNEL_NETWORK_INTERFACE_SEND_UNREACHABLE,           /**< An address no packet leaves for. */
    KERNEL_NETWORK_INTERFACE_SEND_TOO_LONG,              /**< A payload past the card's longest frame. */
    KERNEL_NETWORK_INTERFACE_SEND_ZERO_CHECKSUM_REFUSED, /**< No checksum, from a port not bound to send none. */
    KERNEL_NETWORK_INTERFACE_SEND_STALE_SLOT,            /**< Not the slot reserved, or taken since by an answer. */
    KERNEL_NETWORK_INTERFACE_SEND_REFUSED,               /**< The card's describe refused its length. */
    KERNEL_NETWORK_INTERFACE_SEND_RESULTS                /**< How many results there are. */
} KernelNetworkInterfaceSend_t;

/**
 * @brief What went through an interface.
 */
typedef struct KernelNetworkInterfaceCounters {
    uint32_t frames_received;                                              /**< Frames the poll read. */
    uint32_t fates[KERNEL_NETWORK_INTERFACE_FATES];                        /**< Frames read, by their fate. */
    uint32_t internet_protocol_refused[KERNEL_INTERNET_PROTOCOL_STATUSES]; /**< IPv4 packets refused, by status. */
    uint32_t address_resolution_requests_sent;                             /**< Requests the interface sent. */
    uint32_t address_resolution_requests_unsent;                           /**< Requests it could not publish. */
    uint32_t sends[KERNEL_NETWORK_INTERFACE_SEND_RESULTS];                 /**< Datagrams sent or refused, by result. */
    uint32_t checksums_computed; /**< Payloads the interface summed, given no sum. */
    uint32_t datagrams_common;   /**< Datagrams lent by the common datagram's path, of all those lent. */
} KernelNetworkInterfaceCounters_t;

/**
 * @brief A datagram delivered: lent where the card wrote it, or copied into its port's ring.
 */
typedef struct KernelNetworkDatagram {
    const uint8_t *payload;    /**< Its payload, valid until it is released. */
    uint32_t payload_bytes;    /**< Bytes of it. */
    uint32_t payload_sum;      /**< Its 16-bit words summed, carries not folded: an echo sends it back unsummed. */
    uint32_t source_address;   /**< Its sender's IPv4 address, host order. */
    uint16_t source_port;      /**< Its sender's port. */
    uint16_t destination_port; /**< The port it was sent to. */
    uint32_t arrival;          /**< Its frame's arrival number, which the release of a lent one follows. */
} KernelNetworkDatagram_t;

/**
 * @brief How a port gets its datagrams.
 */
typedef enum KernelNetworkDelivery {
    KERNEL_NETWORK_DELIVERY_LENT = 0, /**< Where the card wrote them, released in their order of arrival. */
    KERNEL_NETWORK_DELIVERY_COPIED,   /**< Copied into the port's own ring. */
} KernelNetworkDelivery_t;

/**
 * @brief A copied port's ring: datagrams[s] describes the copy in buffers + s * buffer_bytes.
 */
typedef struct KernelNetworkPortRing {
    KernelSpscRing_t *ring;             /**< The interface produces, the port's consumer peeks and releases. */
    KernelNetworkDatagram_t *datagrams; /**< One per slot of ring. */
    uint8_t *buffers;                   /**< One buffer of buffer_bytes per slot of ring. */
    uint32_t buffer_bytes;              /**< Bytes of each, the longest payload the port takes. */
} KernelNetworkPortRing_t;

/**
 * @brief A UDP port bound on an interface.
 */
typedef struct KernelNetworkPortBinding {
    uint16_t port;                    /**< Its number, 0 while the place is free. */
    KernelNetworkDelivery_t delivery; /**< How its datagrams come. */
    bool without_checksum;            /**< Takes and sends no checksum: its payload has its own authentication code. */
    KernelNetworkPortRing_t copies;   /**< Its ring, for a copied port. */
} KernelNetworkPortBinding_t;

/**
 * @brief Whether a port was bound, or why not.
 */
typedef enum KernelNetworkInterfaceBind {
    KERNEL_NETWORK_INTERFACE_BIND_BOUND = 0,   /**< Bound. */
    KERNEL_NETWORK_INTERFACE_BIND_PORT_TAKEN,  /**< Already bound. */
    KERNEL_NETWORK_INTERFACE_BIND_PORTS_FULL,  /**< Every place taken. */
    KERNEL_NETWORK_INTERFACE_BIND_BAD_BINDING, /**< Port 0, an unknown delivery, or a copied port without its ring. */
} KernelNetworkInterfaceBind_t;

/**
 * @brief How the checksum of a datagram sent is made.
 */
typedef enum KernelNetworkChecksum {
    KERNEL_NETWORK_CHECKSUM_FROM_SUM = 0, /**< From the payload's sum the caller gives, the payload not read. */
    KERNEL_NETWORK_CHECKSUM_COMPUTED,     /**< The interface sums the payload: the counted fallback. */
    KERNEL_NETWORK_CHECKSUM_NONE,         /**< Zero, from a port bound without checksum only. */
} KernelNetworkChecksum_t;

/**
 * @brief A datagram to send, its payload already written where kernel_network_interface_reserve_datagram said.
 */
typedef struct KernelNetworkDatagramOut {
    uint32_t destination_address;     /**< Host order. */
    uint16_t destination_port;        /**< Its destination port. */
    uint16_t source_port;             /**< Its source port. */
    uint32_t payload_bytes;           /**< Bytes of the payload written. */
    uint32_t payload_sum;             /**< With KERNEL_NETWORK_CHECKSUM_FROM_SUM, its 16-bit words summed. */
    KernelNetworkChecksum_t checksum; /**< How its checksum is made. */
} KernelNetworkDatagramOut_t;

/**
 * @brief What the path of the common datagram compares a frame against, each value as a load from a frame reads it.
 */
typedef struct KernelNetworkInterfaceWire {
    uint32_t station_head;                               /**< The card's station address, its first 4 bytes. */
    uint16_t station_tail;                               /**< Its last 2. */
    uint32_t address;                                    /**< The interface's IPv4 address. */
    uint16_t lent_ports[KERNEL_NETWORK_INTERFACE_PORTS]; /**< Each place's port when bound lent, else 0. */
    uint32_t pseudo_header_sum; /**< The interface's address and UDP's protocol, as a pseudo-header sums them. */
    uint32_t subnet_network;    /**< The subnet's network address, host order. */
    uint32_t subnet_broadcast;  /**< Its directed broadcast, host order. */
    bool has_broadcast;         /**< Whether the subnet has both, a /30 or wider. */
} KernelNetworkInterfaceWire_t;

/**
 * @brief An interface's addresses and times.
 */
typedef struct KernelNetworkInterfaceConfig {
    uint32_t address;          /**< Its IPv4 address, host order. */
    uint32_t netmask;          /**< Its netmask. */
    uint32_t gateway;          /**< The next hop off the subnet, 0 for none. */
    uint32_t cache_lifetime;   /**< How long a learned station lasts, in the unit of the times calls take. */
    uint32_t request_interval; /**< Least time between two ARP requests a send makes for one next hop. */
} KernelNetworkInterfaceConfig_t;

/**
 * @brief An IPv4 interface on one network card.
 */
typedef struct KernelNetworkInterface {
    KernelNetworkDevice_t *device;        /**< The card. */
    uint32_t address;                     /**< The interface's IPv4 address, host order. */
    uint32_t netmask;                     /**< Its netmask, host order. */
    uint32_t gateway;                     /**< The next hop off the subnet, 0 for none. */
    uint32_t request_interval;            /**< Least time between two ARP requests a send makes for a hop. */
    uint16_t next_identification;         /**< The identification of the next IPv4 packet sent. */
    bool transmit_exhausted;              /**< Whether a kick this batch freed no slot: it sends no more. */
    bool datagram_reserved;               /**< Whether a transmit slot is reserved for a datagram. */
    uint32_t reserved_slot;               /**< That slot. */
    uint32_t receive_taken;               /**< Frames past the card's oldest held that the poll took. */
    uint32_t receive_released;            /**< Frames given back to the card: the oldest held's arrival. */
    uint32_t arrival;                     /**< The arrival number of the frame being taken. */
    KernelAddressResolutionCache_t cache; /**< The station addresses learned. */
    uint32_t station_hints[KERNEL_NETWORK_INTERFACE_STATION_HINTS];   /**< The cache entry a send last found, by hop. */
    KernelNetworkPortBinding_t ports[KERNEL_NETWORK_INTERFACE_PORTS]; /**< The ports bound. */
    KernelNetworkInterfaceWire_t wire;                                /**< The common datagram's values. */
    KernelSpscRing_t lent_ring;                                       /**< Lent datagrams, oldest first. */
    KernelNetworkDatagram_t lent[KERNEL_NETWORK_INTERFACE_LENT_DATAGRAMS]; /**< One per slot of lent_ring. */
    KernelNetworkInterfaceCounters_t counters;                             /**< What went through. */
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
 * @brief Sets an interface up on @p device, with @p config's addresses, an empty ARP cache and no port bound.
 *
 * @param interface The interface.
 * @param device    A ready card.
 * @param config    Its addresses and times.
 * @return KERNEL_NETWORK_INTERFACE_READY, or why @p device is refused, @p interface then untouched.
 */
extern KernelNetworkInterfaceSetup_t kernel_network_interface_initialize(KernelNetworkInterface_t *interface,
                                                                         KernelNetworkDevice_t *device,
                                                                         const KernelNetworkInterfaceConfig_t *config);

/**
 * @brief Binds @p binding's port, whose datagrams the poll then delivers as it says.
 *
 * @param interface The interface.
 * @param binding   The port, its delivery, and its ring when copied.
 * @return KERNEL_NETWORK_INTERFACE_BIND_BOUND, or why the port was not bound.
 */
extern KernelNetworkInterfaceBind_t kernel_network_interface_bind(KernelNetworkInterface_t *interface,
                                                                  const KernelNetworkPortBinding_t *binding);

/**
 * @brief Lent datagrams the consumer has not released.
 *
 * @param interface The interface.
 * @return How many, from the oldest.
 */
extern uint32_t kernel_network_interface_lent_count(KernelNetworkInterface_t *interface);

/**
 * @brief The lent datagram @p offset places after the oldest.
 *
 * @param interface The interface.
 * @param offset    Below what kernel_network_interface_lent_count last returned.
 * @return The datagram, its payload where the card wrote it until it is released.
 */
extern const KernelNetworkDatagram_t *kernel_network_interface_lent_at(const KernelNetworkInterface_t *interface,
                                                                       uint32_t offset);

/**
 * @brief Releases the @p count oldest lent datagrams, and gives the card back every frame no lent datagram holds.
 *
 * @param interface The interface.
 * @param count     How many.
 * @return How many were released: at most those lent.
 */
extern uint32_t kernel_network_interface_release_lent(KernelNetworkInterface_t *interface, uint32_t count);

/**
 * @brief Reserves a transmit slot for a datagram, whose payload the caller then writes where the headers end.
 *
 * @details Nothing else may send on the interface between this call and kernel_network_interface_send_datagram:
 *          an answer the poll sends meanwhile takes the slot, and the send then refuses it as stale.
 *
 * @param interface The interface.
 * @param out_slot  Receives the slot.
 * @param out_room  Receives the bytes the payload may take.
 * @return Where the payload goes, KERNEL_NETWORK_INTERFACE_HEADERS_BYTES into the slot's buffer, or NULL when no
 *         slot is free.
 */
extern uint8_t *kernel_network_interface_reserve_datagram(KernelNetworkInterface_t *interface, uint32_t *out_slot,
                                                          uint32_t *out_room);

/**
 * @brief Writes the headers of @p datagram in front of its payload in @p slot and publishes it.
 *
 * @param interface The interface.
 * @param slot      The slot kernel_network_interface_reserve_datagram gave.
 * @param datagram  Its destination, ports, payload length and checksum.
 * @param now       The caller's time.
 * @return KERNEL_NETWORK_INTERFACE_SEND_SENT, or why the datagram was not sent, its slot then left unpublished.
 */
extern KernelNetworkInterfaceSend_t kernel_network_interface_send_datagram(KernelNetworkInterface_t *interface,
                                                                           uint32_t slot,
                                                                           const KernelNetworkDatagramOut_t *datagram,
                                                                           uint32_t now);

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
