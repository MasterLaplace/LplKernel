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
 * @file icmp.h
 * @brief ICMP for IPv4 (RFC 792): an echo request recognised, and its reply written.
 *
 * The stack answers echo and nothing else: a ping reaches it, and no other message changes what it does.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-22
 **************************************************************************/

#ifndef KERNEL_NET_ICMP_H_
#define KERNEL_NET_ICMP_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name ICMP
 * @{
 */
#define KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REPLY   0u /**< Type of an echo reply. */
#define KERNEL_INTERNET_CONTROL_MESSAGE_ECHO_REQUEST 8u /**< Type of an echo request. */
#define KERNEL_INTERNET_CONTROL_MESSAGE_HEADER_BYTES 8u /**< Type, code, checksum, identifier and sequence. */
/** @} */

/**
 * @brief Whether the message of @p length bytes at @p bytes is an echo request whose checksum holds.
 *
 * @param bytes  The message, the IPv4 payload.
 * @param length Its bytes.
 * @return true for a sound echo request.
 */
extern bool kernel_internet_control_message_is_echo_request(const uint8_t *bytes, uint32_t length);

/**
 * @brief Writes at @p reply the echo reply to the request of @p length bytes at @p request.
 *
 * @details The reply carries the request's identifier, sequence and data, and its own checksum.
 *
 * @param reply   Where the reply goes; @p length bytes follow.
 * @param request The request.
 * @param length  Its bytes.
 */
extern void kernel_internet_control_message_write_echo_reply(uint8_t *reply, const uint8_t *request, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_ICMP_H_ */
