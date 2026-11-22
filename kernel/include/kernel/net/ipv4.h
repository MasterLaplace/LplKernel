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
 * @file ipv4.h
 * @brief IPv4 (RFC 791): a header checked and read where it arrived, and written in front of a payload.
 *
 * A packet the stack refuses gets a status that says why, so that each refusal is counted by its cause. A fragment
 * is refused: the stack reassembles none, and a datagram the engine sends fits one frame.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_IPV4_H_
#define KERNEL_NET_IPV4_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name IPv4
 * @{
 */
#define KERNEL_INTERNET_PROTOCOL_VERSION_4               4u      /**< Version, in the high nibble of the first byte. */
#define KERNEL_INTERNET_PROTOCOL_DONT_FRAGMENT           0x4000u /**< DF, set on every packet sent. */
#define KERNEL_INTERNET_PROTOCOL_HEADER_BYTES            20u     /**< A header without options. */
#define KERNEL_INTERNET_PROTOCOL_CONTROL_MESSAGE         1u      /**< Protocol number of ICMP. */
#define KERNEL_INTERNET_PROTOCOL_USER_DATAGRAM           17u     /**< Protocol number of UDP. */
#define KERNEL_INTERNET_PROTOCOL_TIME_TO_LIVE            64u     /**< TTL of a packet sent. */
#define KERNEL_INTERNET_PROTOCOL_MULTICAST_TIME_TO_LIVE  1u      /**< TTL of one to a group: RFC 1112, 6.1. */
#define KERNEL_INTERNET_PROTOCOL_LOOPBACK_NETWORK        127u    /**< First byte of the loopback addresses. */
#define KERNEL_INTERNET_PROTOCOL_FIRST_MULTICAST_NETWORK 224u    /**< First byte of 224/4, 240/4 above it. */
#define KERNEL_INTERNET_PROTOCOL_LIMITED_BROADCAST       0xFFFFFFFFu /**< 255.255.255.255. */
/** @} */

/**
 * @brief Why a packet was taken or refused.
 */
typedef enum KernelInternetProtocolStatus {
    KERNEL_INTERNET_PROTOCOL_TAKEN = 0,         /**< A whole packet, its header sound. */
    KERNEL_INTERNET_PROTOCOL_TRUNCATED,         /**< Shorter than its header or its total length says. */
    KERNEL_INTERNET_PROTOCOL_NOT_VERSION_4,     /**< Another version. */
    KERNEL_INTERNET_PROTOCOL_BAD_HEADER_LENGTH, /**< IHL under 5 words. */
    KERNEL_INTERNET_PROTOCOL_BAD_TOTAL_LENGTH,  /**< A total length shorter than its own header. */
    KERNEL_INTERNET_PROTOCOL_BAD_CHECKSUM,      /**< Its header's checksum does not hold. */
    KERNEL_INTERNET_PROTOCOL_FRAGMENT,          /**< A fragment, which the stack does not reassemble. */
    KERNEL_INTERNET_PROTOCOL_STATUSES           /**< How many statuses there are. */
} KernelInternetProtocolStatus_t;

/**
 * @brief A packet's header, its addresses in host order, read in place.
 */
typedef struct KernelInternetProtocolPacket {
    uint32_t source;        /**< Its source address. */
    uint32_t destination;   /**< Its destination address. */
    uint8_t protocol;       /**< What its payload is. */
    const uint8_t *payload; /**< Its payload, after the header and its options. */
    uint32_t payload_bytes; /**< Bytes of it, as its total length says. */
} KernelInternetProtocolPacket_t;

/**
 * @brief The three addresses a source check refuses on an interface's subnet, computed once, all three in the byte
 *        order of the source compared with them.
 */
typedef struct KernelInternetProtocolSubnet {
    uint32_t address;   /**< The interface's own address. */
    uint32_t network;   /**< Its subnet's network address, or its own address where the subnet has none. */
    uint32_t broadcast; /**< Its subnet's directed broadcast, or its own address where the subnet has none. */
} KernelInternetProtocolSubnet_t;

/**
 * @brief Checks and reads the packet of @p length bytes at @p bytes.
 *
 * @param bytes  The packet, after its Ethernet header.
 * @param length Its bytes, padding included.
 * @param out    Receives its header and where its payload lies, when taken.
 * @return KERNEL_INTERNET_PROTOCOL_TAKEN, or why the packet is refused, @p out then untouched.
 */
extern KernelInternetProtocolStatus_t kernel_internet_protocol_parse(const uint8_t *bytes, uint32_t length,
                                                                     KernelInternetProtocolPacket_t *out);

/**
 * @brief Whether @p source may send to an interface at @p address in @p netmask's subnet (RFC 1122, 3.2.1.3).
 *
 * @details Refuses what no host sends from: this network ({0, any}), loopback (127/8), a multicast or reserved
 *          address (224/4 and 240/4, the limited broadcast with them), the subnet's directed broadcast, and the
 *          interface's own address, which only a loop or a forger sends back to it. The subnet's network address
 *          is refused with its broadcast, a /31 or /32 having neither.
 *
 * @param source  The packet's source address, host order.
 * @param address The interface's address.
 * @param netmask Its netmask.
 * @return true for a source a datagram may come from.
 */
extern bool kernel_internet_protocol_source_is_acceptable(uint32_t source, uint32_t address, uint32_t netmask);

/**
 * @brief The subnet of an interface at @p address with @p netmask, host order.
 *
 * @details A /31 or /32 has no network address and no broadcast to refuse: both are the interface's own address,
 *          which is refused anyway.
 *
 * @param address The interface's address, host order.
 * @param netmask Its netmask.
 * @return Its address, its subnet's network address and its directed broadcast.
 */
static inline KernelInternetProtocolSubnet_t kernel_internet_protocol_subnet(uint32_t address, uint32_t netmask);

/**
 * @brief What kernel_internet_protocol_source_is_acceptable decides, on a subnet computed once, in either byte
 *        order: @p source and @p subnet in the same one.
 *
 * @param first_byte The source's first byte on the wire.
 * @param source     The source address, in the byte order of @p subnet.
 * @param subnet     The interface's subnet, from kernel_internet_protocol_subnet or the same in another order.
 * @return true for a source a datagram may come from.
 */
static inline bool kernel_internet_protocol_source_is_acceptable_in(uint8_t first_byte, uint32_t source,
                                                                    const KernelInternetProtocolSubnet_t *subnet);

/**
 * @brief Writes a header without options, its checksum computed, in front of a payload of @p payload_bytes.
 *
 * @param bytes          The header's first byte; KERNEL_INTERNET_PROTOCOL_HEADER_BYTES follow.
 * @param source         The source address.
 * @param destination    The destination address.
 * @param protocol       What the payload is.
 * @param payload_bytes  Bytes of the payload.
 * @param identification The packet's identification.
 */
static inline void kernel_internet_protocol_write_header(uint8_t *bytes, uint32_t source, uint32_t destination,
                                                         uint8_t protocol, uint32_t payload_bytes,
                                                         uint16_t identification);

/**
 * @brief Sets the time to live of the header at @p bytes, its checksum updated in constant time from the word that
 *        changed (RFC 1624, equation 3), as a router does when it lowers one.
 *
 * @param bytes        The header's first byte.
 * @param time_to_live The new time to live.
 */
static inline void kernel_internet_protocol_set_time_to_live(uint8_t *bytes, uint8_t time_to_live);

#include <kernel/net/ipv4.inl>

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_IPV4_H_ */
