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
 * @file arp.h
 * @brief ARP over Ethernet for IPv4 (RFC 826): its packets read and written, and a cache of fixed size.
 *
 * The cache counts no wall clock: each call takes the caller's time, in whatever unit its lifetime is given
 * (a tick, for the stack), so that a replay of the same calls learns and forgets the same entries.
 *
 * An entry is wanted once the interface asked for its address: the gateway, a peer it sends to. An entry learned
 * from a station that only talked to the interface is not, and never evicts a wanted one, so that a flood of
 * requests from made-up senders cannot push the gateway out. A reply creates no entry: it fills one the interface
 * asked for, or refreshes one it holds, as RFC 826's merge does for every packet whose sender it knows.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_ARP_H_
#define KERNEL_NET_ARP_H_

#include <kernel/net/device.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name ARP for IPv4 over Ethernet
 * @{
 */
#define KERNEL_ADDRESS_RESOLUTION_BYTES         28u /**< A packet: 8 bytes of header, two addresses of each kind. */
#define KERNEL_ADDRESS_RESOLUTION_REQUEST       1u  /**< Operation of a request. */
#define KERNEL_ADDRESS_RESOLUTION_REPLY         2u  /**< Operation of a reply. */
#define KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES 16u /**< Entries of a cache. */
/** @} */

/**
 * @brief An ARP packet, its IPv4 addresses in host order.
 */
typedef struct KernelAddressResolutionPacket {
    uint16_t operation;                                   /**< KERNEL_ADDRESS_RESOLUTION_REQUEST or _REPLY. */
    uint8_t sender_station[KERNEL_NETWORK_ADDRESS_BYTES]; /**< The sender's station address. */
    uint32_t sender_address;                              /**< The sender's IPv4 address. */
    uint8_t target_station[KERNEL_NETWORK_ADDRESS_BYTES]; /**< The target's station address, zero in a request. */
    uint32_t target_address;                              /**< The target's IPv4 address. */
} KernelAddressResolutionPacket_t;

/**
 * @brief One address, asked for or learned.
 */
typedef struct KernelAddressResolutionEntry {
    uint32_t address;                              /**< The IPv4 address, 0 while the entry is free. */
    uint8_t station[KERNEL_NETWORK_ADDRESS_BYTES]; /**< Its station address, once resolved. */
    bool resolved;                                 /**< Whether station holds it yet. */
    bool wanted;                                   /**< Whether the interface asked for it. */
    uint32_t since;                                /**< The caller's time it was learned at, or asked at unresolved. */
} KernelAddressResolutionEntry_t;

/**
 * @brief A cache of learned addresses.
 */
typedef struct KernelAddressResolutionCache {
    KernelAddressResolutionEntry_t entries[KERNEL_ADDRESS_RESOLUTION_CACHE_ENTRIES]; /**< Its entries. */
    uint32_t lifetime;                                                               /**< How long an entry lasts. */
} KernelAddressResolutionCache_t;

/**
 * @brief Reads the ARP packet of @p length bytes at @p bytes.
 *
 * @param bytes  The packet, after its Ethernet header.
 * @param length Its bytes.
 * @param out    Receives the packet.
 * @return false, @p out untouched, for a packet too short or of another hardware, protocol or address size.
 */
extern bool kernel_address_resolution_parse(const uint8_t *bytes, uint32_t length,
                                            KernelAddressResolutionPacket_t *out);

/**
 * @brief Writes @p packet at @p bytes.
 *
 * @param bytes  Its first byte; KERNEL_ADDRESS_RESOLUTION_BYTES follow.
 * @param packet The packet.
 */
extern void kernel_address_resolution_write(uint8_t *bytes, const KernelAddressResolutionPacket_t *packet);

/**
 * @brief Empties @p cache and sets how long its entries last.
 *
 * @param cache    The cache.
 * @param lifetime How long an entry lasts, in the unit of the times its calls take.
 */
extern void kernel_address_resolution_cache_initialize(KernelAddressResolutionCache_t *cache, uint32_t lifetime);

/**
 * @brief Marks @p address as wanted, so that a reply about it is taken, with an entry for it if it has none.
 *
 * @details An entry still resolved within its lifetime keeps its station; any other starts again unresolved at
 *          @p now. A new entry takes a free or expired one, failing that the oldest not wanted, failing that the
 *          oldest.
 *
 * @param cache   The cache.
 * @param address An IPv4 address other than 0.
 * @param now     The caller's time.
 */
extern void kernel_address_resolution_cache_ask(KernelAddressResolutionCache_t *cache, uint32_t address, uint32_t now);

/**
 * @brief Learns that @p address is at @p station, if the cache holds an entry for it: RFC 826's merge.
 *
 * @param cache   The cache.
 * @param address An IPv4 address other than 0.
 * @param station Its station address.
 * @param now     The caller's time.
 * @return false, the cache untouched, when it holds no entry for @p address.
 */
extern bool kernel_address_resolution_cache_merge(KernelAddressResolutionCache_t *cache, uint32_t address,
                                                  const uint8_t *station, uint32_t now);

/**
 * @brief Learns that @p address, which the cache holds no entry for, is at @p station, in an entry not wanted.
 *
 * @param cache   The cache.
 * @param address An IPv4 address other than 0, which the cache holds no entry for.
 * @param station Its station address.
 * @param now     The caller's time.
 * @return false, the cache untouched, when every entry is wanted and within its lifetime.
 */
extern bool kernel_address_resolution_cache_add(KernelAddressResolutionCache_t *cache, uint32_t address,
                                                const uint8_t *station, uint32_t now);

/**
 * @brief Finds the station address of @p address, unless it is unresolved or was learned a lifetime ago or more.
 *
 * @param cache       The cache.
 * @param address     The IPv4 address.
 * @param now         The caller's time.
 * @param out_station Receives the station address.
 * @return false, @p out_station untouched, when @p address is unknown, unresolved or its entry expired.
 */
extern bool kernel_address_resolution_cache_find(const KernelAddressResolutionCache_t *cache, uint32_t address,
                                                 uint32_t now, uint8_t *out_station);

/**
 * @brief Whether @p address was asked for less than @p interval ago and is still unresolved: a reply is on its way,
 *        or soon to be given up on.
 *
 * @param cache    The cache.
 * @param address  The IPv4 address.
 * @param now      The caller's time.
 * @param interval How long a request is waited for.
 * @return true while another request for @p address would come too soon.
 */
extern bool kernel_address_resolution_cache_is_pending(const KernelAddressResolutionCache_t *cache, uint32_t address,
                                                       uint32_t now, uint32_t interval);

/**
 * @brief Frees every entry learned, or asked for, a lifetime ago or more.
 *
 * @details Called once a batch, it keeps an entry nobody looks at from coming back to life when the caller's
 *          time wraps: an age is a difference of 32-bit times, so it must be read before it passes 2^32.
 *
 * @param cache The cache.
 * @param now   The caller's time.
 */
extern void kernel_address_resolution_cache_expire(KernelAddressResolutionCache_t *cache, uint32_t now);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_ARP_H_ */
