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
 * @file ethernet.h
 * @brief Ethernet II frames: their header read where the card wrote it, and written in front of a payload.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_ETHERNET_H_
#define KERNEL_NET_ETHERNET_H_

#include <kernel/net/device.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Ethernet II
 * @{
 */
#define KERNEL_ETHERNET_HEADER_BYTES                     14u     /**< Destination, source and EtherType. */
#define KERNEL_ETHERNET_FRAME_LEAST_BYTES                60u     /**< Shortest frame on the wire, its CRC aside. */
#define KERNEL_ETHERNET_TYPE_INTERNET_PROTOCOL_VERSION_4 0x0800u /**< EtherType of IPv4. */
#define KERNEL_ETHERNET_TYPE_ADDRESS_RESOLUTION          0x0806u /**< EtherType of ARP. */
/** @} */

/**
 * @brief A frame's header, read in place.
 */
typedef struct KernelEthernetFrame {
    const uint8_t *destination; /**< Its destination address, in the frame. */
    const uint8_t *source;      /**< Its source address, in the frame. */
    uint16_t type;              /**< Its EtherType. */
    const uint8_t *payload;     /**< What follows the header. */
    uint32_t payload_bytes;     /**< Bytes of it. */
} KernelEthernetFrame_t;

/**
 * @brief The broadcast station address, every bit set.
 */
extern const uint8_t kernel_ethernet_broadcast_station[KERNEL_NETWORK_ADDRESS_BYTES];

/**
 * @brief Reads the header of the frame of @p length bytes at @p bytes.
 *
 * @param bytes  The frame.
 * @param length Its bytes.
 * @param out    Receives the header and where the payload lies.
 * @return false, @p out untouched, for a frame shorter than its header.
 */
extern bool kernel_ethernet_parse(const uint8_t *bytes, uint32_t length, KernelEthernetFrame_t *out);

/**
 * @brief Whether a frame is addressed to @p address or to every station.
 *
 * @param frame   The frame's header.
 * @param address The station's own address.
 * @return true for the station's address or the broadcast address.
 */
extern bool kernel_ethernet_is_for(const KernelEthernetFrame_t *frame, const uint8_t *address);

/**
 * @brief Whether @p station is a group address, multicast or broadcast: the low bit of its first byte (IEEE 802).
 *
 * @param station A station address.
 * @return true for a group address, which never sends a frame.
 */
static inline bool kernel_ethernet_is_group(const uint8_t *station);

/**
 * @brief Writes a header in front of a payload.
 *
 * @param bytes       The frame's first byte.
 * @param destination Its destination address.
 * @param source      Its source address.
 * @param type        Its EtherType.
 */
static inline void kernel_ethernet_write_header(uint8_t *bytes, const uint8_t *destination, const uint8_t *source,
                                                uint16_t type);

#include <kernel/net/ethernet.inl>

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_ETHERNET_H_ */
