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
 * @file udp.h
 * @brief UDP (RFC 768): a datagram checked and read where it arrived, and its header written in front of a payload.
 *
 * A datagram's payload is summed once, as it is checked, and the sum is kept whole, its carries not folded: a
 * caller that sends the same payload back, or to many destinations, adds each one's pseudo-header and header
 * to it and folds once, without summing the payload again.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_UDP_H_
#define KERNEL_NET_UDP_H_

#include <kernel/net/ipv4.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KERNEL_USER_DATAGRAM_HEADER_BYTES 8u /**< Source port, destination port, length and checksum. */

/**
 * @brief Why a datagram was taken or refused.
 */
typedef enum KernelUserDatagramStatus {
    KERNEL_USER_DATAGRAM_TAKEN = 0,    /**< A whole datagram whose checksum holds. */
    KERNEL_USER_DATAGRAM_TRUNCATED,    /**< An IPv4 payload shorter than a header. */
    KERNEL_USER_DATAGRAM_BAD_LENGTH,   /**< A length under a header, or past its IPv4 payload. */
    KERNEL_USER_DATAGRAM_BAD_CHECKSUM, /**< A checksum that does not hold. */
    KERNEL_USER_DATAGRAM_NO_CHECKSUM,  /**< A checksum of zero: its sender computed none. */
    KERNEL_USER_DATAGRAM_STATUSES      /**< How many statuses there are. */
} KernelUserDatagramStatus_t;

/**
 * @brief A datagram's header, read in place, and its payload's sum.
 */
typedef struct KernelUserDatagram {
    uint16_t source_port;      /**< Its source port. */
    uint16_t destination_port; /**< Its destination port. */
    const uint8_t *payload;    /**< Its payload, where it arrived. */
    uint32_t payload_bytes;    /**< Bytes of it, as its length says. */
    uint32_t payload_sum;      /**< The payload's 16-bit words summed, carries not folded. */
} KernelUserDatagram_t;

/**
 * @brief The sum of a datagram's header words but its checksum: its ports and its length.
 *
 * @param source_port      Its source port.
 * @param destination_port Its destination port.
 * @param datagram_bytes   Its length, its header included.
 * @return The sum.
 */
static inline uint32_t kernel_user_datagram_header_sum(uint16_t source_port, uint16_t destination_port,
                                                       uint32_t datagram_bytes);

/**
 * @brief The sum of the pseudo-header RFC 768 puts in front of a datagram, carries not folded.
 *
 * @param source         The IPv4 source address, host order.
 * @param destination    The IPv4 destination address, host order.
 * @param datagram_bytes The datagram's length, its header included.
 * @return The sum.
 */
static inline uint32_t kernel_user_datagram_pseudo_header_sum(uint32_t source, uint32_t destination,
                                                              uint32_t datagram_bytes);

/**
 * @brief Checks and reads the datagram that @p packet carries.
 *
 * @details Bytes of the IPv4 payload past the datagram's own length are left out, as padding.
 *
 * @param packet The IPv4 packet, its protocol UDP.
 * @param out    Receives the datagram, when taken and when its checksum is zero.
 * @return KERNEL_USER_DATAGRAM_TAKEN, KERNEL_USER_DATAGRAM_NO_CHECKSUM with @p out filled, its payload unchecked,
 *         or why the datagram is refused, @p out then untouched.
 */
extern KernelUserDatagramStatus_t kernel_user_datagram_parse(const KernelInternetProtocolPacket_t *packet,
                                                             KernelUserDatagram_t *out);

/**
 * @brief The checksum of a datagram, from its pseudo-header's sum, its header and its payload's sum.
 *
 * @param pseudo_header_sum What kernel_user_datagram_pseudo_header_sum gives for the datagram.
 * @param source_port       Its source port.
 * @param destination_port  Its destination port.
 * @param payload_bytes     Bytes of its payload.
 * @param payload_sum       Its payload's 16-bit words summed, carries not folded.
 * @return The checksum, FFFFh where the sum folds to zero, since zero says that none was computed (RFC 768).
 */
static inline uint16_t kernel_user_datagram_checksum(uint32_t pseudo_header_sum, uint16_t source_port,
                                                     uint16_t destination_port, uint32_t payload_bytes,
                                                     uint32_t payload_sum);

/**
 * @brief Writes a datagram's header in front of a payload of @p payload_bytes.
 *
 * @param bytes            The header's first byte; KERNEL_USER_DATAGRAM_HEADER_BYTES follow.
 * @param source_port      Its source port.
 * @param destination_port Its destination port.
 * @param payload_bytes    Bytes of its payload.
 * @param checksum         Its checksum, or zero for none.
 */
static inline void kernel_user_datagram_write_header(uint8_t *bytes, uint16_t source_port, uint16_t destination_port,
                                                     uint32_t payload_bytes, uint16_t checksum);

#include <kernel/net/udp.inl>

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_UDP_H_ */
