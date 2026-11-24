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
 * @file byte_order.h
 * @brief Network byte order: big-endian fields read and written a byte at a time, whatever their alignment.
 *
 * A frame's fields sit at any offset, so they are read and written byte by byte; GCC merges the bytes into one
 * load or store and a byte swap where the target allows it.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_BYTE_ORDER_H_
#define KERNEL_NET_BYTE_ORDER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The big-endian 16-bit field at @p bytes.
 *
 * @param bytes Its first byte.
 * @return Its value.
 */
static inline uint16_t kernel_network_read_16(const uint8_t *bytes);

/**
 * @brief The big-endian 32-bit field at @p bytes.
 *
 * @param bytes Its first byte.
 * @return Its value.
 */
static inline uint32_t kernel_network_read_32(const uint8_t *bytes);

/**
 * @brief Writes @p value as a big-endian 16-bit field at @p bytes.
 *
 * @param bytes Its first byte.
 * @param value The value.
 */
static inline void kernel_network_write_16(uint8_t *bytes, uint16_t value);

/**
 * @brief Writes @p value as a big-endian 32-bit field at @p bytes.
 *
 * @param bytes Its first byte.
 * @param value The value.
 */
static inline void kernel_network_write_32(uint8_t *bytes, uint32_t value);

/**
 * @brief The 2 bytes at @p bytes as the host holds them, whatever their alignment: a field compared or summed
 *        without a byte swap.
 *
 * @param bytes Its first byte.
 * @return Its bytes, in the host's order.
 */
static inline uint16_t kernel_network_load_16(const uint8_t *bytes);

/**
 * @brief The 4 bytes at @p bytes as the host holds them, whatever their alignment: a field compared or summed
 *        without a byte swap.
 *
 * @param bytes Its first byte.
 * @return Its bytes, in the host's order.
 */
static inline uint32_t kernel_network_load_32(const uint8_t *bytes);

#include <kernel/net/byte_order.inl>

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_BYTE_ORDER_H_ */
