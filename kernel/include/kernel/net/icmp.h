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
 * @brief ICMP for IPv4 (RFC 792): a message checked where it arrived, and the reply to an echo request written.
 *
 * The stack answers echo and nothing else: a ping reaches it, and no other message changes what it does. The sum
 * that checks a message is kept whole, its carries not folded, so that the reply's checksum follows from it by
 * taking out the request's type and checksum: no second pass over the data, and the same checksum as a sum from
 * scratch, the zero of a message of zeros included, which an update of the folded checksum by RFC 1624's
 * equation 3 gives as 0000h instead of FFFFh.
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
 * @brief A message's header, read in place, and the sum that checked it.
 */
typedef struct KernelInternetControlMessage {
    uint8_t type;         /**< What the message is. */
    uint8_t code;         /**< Its subtype. */
    uint16_t checksum;    /**< Its checksum, as it arrived. */
    uint32_t sum;         /**< Its 16-bit words summed, the checksum's included, carries not folded. */
    const uint8_t *bytes; /**< The message, where it arrived. */
    uint32_t length;      /**< Its bytes. */
} KernelInternetControlMessage_t;

/**
 * @brief Checks and reads the message of @p length bytes at @p bytes.
 *
 * @param bytes  The message, the IPv4 payload.
 * @param length Its bytes.
 * @param out    Receives its header and its sum.
 * @return false, @p out untouched, for a message shorter than its header or whose checksum does not hold.
 */
extern bool kernel_internet_control_message_parse(const uint8_t *bytes, uint32_t length,
                                                  KernelInternetControlMessage_t *out);

/**
 * @brief Whether @p message is an echo request: type 8, code 0.
 *
 * @param message A message kernel_internet_control_message_parse took.
 * @return true for an echo request.
 */
extern bool kernel_internet_control_message_is_echo_request(const KernelInternetControlMessage_t *message);

/**
 * @brief Writes at @p reply the echo reply to @p request.
 *
 * @details The reply carries the request's identifier, sequence and data; its checksum is the request's sum
 *          without the request's checksum and type, folded: the checksum a sum of the reply from scratch gives.
 *
 * @param reply   Where the reply goes; @p request->length bytes follow. It may be the request itself.
 * @param request An echo request kernel_internet_control_message_parse took.
 */
extern void kernel_internet_control_message_write_echo_reply(uint8_t *reply,
                                                             const KernelInternetControlMessage_t *request);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_NET_ICMP_H_ */
