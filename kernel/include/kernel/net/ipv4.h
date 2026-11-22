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
#define KERNEL_INTERNET_PROTOCOL_HEADER_BYTES 20u /**< A header without options. */
#define KERNEL_INTERNET_PROTOCOL_ICMP         1u  /**< Protocol number of ICMP. */
#define KERNEL_INTERNET_PROTOCOL_UDP          17u /**< Protocol number of UDP. */
#define KERNEL_INTERNET_PROTOCOL_TIME_TO_LIVE 64u /**< TTL of a packet sent. */
/** @} */

/**
 * @brief Why a packet was taken or refused.
 */
typedef enum KernelInternetProtocolStatus {
    KERNEL_INTERNET_PROTOCOL_TAKEN = 0,         /**< A whole packet, its header sound. */
    KERNEL_INTERNET_PROTOCOL_TRUNCATED,         /**< Shorter than its header or its total length says. */
    KERNEL_INTERNET_PROTOCOL_NOT_VERSION_4,     /**< Another version. */
    KERNEL_INTERNET_PROTOCOL_BAD_HEADER_LENGTH, /**< IHL under 5 words. */
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
 * @brief Writes a header without options, its checksum computed, in front of a payload of @p payload_bytes.
 *
 * @param bytes          The header's first byte; KERNEL_INTERNET_PROTOCOL_HEADER_BYTES follow.
 * @param source         The source address.
 * @param destination    The destination address.
 * @param protocol       What the payload is.
 * @param payload_bytes  Bytes of the payload.
 * @param identification The packet's identification.
 */
extern void kernel_internet_protocol_write_header(uint8_t *bytes, uint32_t source, uint32_t destination,
                                                  uint8_t protocol, uint32_t payload_bytes, uint16_t identification);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_IPV4_H_ */
