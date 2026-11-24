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
 * @file checksum.h
 * @brief The Internet checksum: the ones' complement of the ones' complement sum of 16-bit words (RFC 1071).
 *
 * A sum is built in parts, so that a header and a payload summed apart, or a payload summed once and sent to
 * many, add up as one: kernel_internet_checksum_add adds bytes to a running sum, exactly, and
 * kernel_internet_checksum_finish folds its carries and complements it.
 *
 * Any sum congruent to the exact one modulo FFFFh gives the same checksum once a nonzero part is added, since a
 * fold maps every positive multiple of FFFFh to FFFFh. kernel_internet_checksum_fold_bytes sums that way: the
 * words in the host's own order, four bytes a load, folded and turned big-endian once at the end (RFC 1071,
 * 2(B), byte order independence), for a header checked or a payload whose sum goes on to a checksum. Only a sum
 * that something is later taken out of, such as an echo request's type, needs the exact one.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_CHECKSUM_H_
#define KERNEL_NET_CHECKSUM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Adds @p length bytes, as big-endian 16-bit words, to the running sum @p sum.
 *
 * @details An odd last byte counts as the high byte of a word whose low byte is zero, so a part of odd length
 *          is only ever the last one of a sum.
 *
 * @param sum    The running sum, 0 to start.
 * @param bytes  The bytes.
 * @param length How many.
 * @return The running sum, carries not yet folded.
 */
extern uint32_t kernel_internet_checksum_add(uint32_t sum, const uint8_t *bytes, uint32_t length);

/**
 * @brief The ones' complement sum of @p length bytes as big-endian 16-bit words, folded to 16 bits.
 *
 * @details Congruent to what kernel_internet_checksum_add gives modulo FFFFh, and zero only for bytes all zero.
 *          An odd last byte counts as the high byte of a word whose low byte is zero.
 *
 * @param bytes  The bytes, at any alignment.
 * @param length How many.
 * @return The folded sum, in host order.
 */
extern uint16_t kernel_internet_checksum_fold_bytes(const uint8_t *bytes, uint32_t length);

/**
 * @brief Folds the carries of @p sum and complements it: the checksum a header carries.
 *
 * @param sum A running sum.
 * @return The checksum, in host order, to write big-endian.
 */
static inline uint16_t kernel_internet_checksum_finish(uint32_t sum);

#include <kernel/net/checksum.inl>

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_CHECKSUM_H_ */
