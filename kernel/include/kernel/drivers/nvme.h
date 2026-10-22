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
 * @file nvme.h
 * @brief NVM Express controller: found on the bus, reset, and identified.
 *
 * The first step of the driver: the controller is brought out of reset with its admin queue
 * pair, and answers Identify for itself and for its first namespace. Everything is polled; no
 * interrupt is used. Everything here is spelled out in `non_volatile_memory_express_*` per the
 * naming convention; the file name keeps the acronym, which is the documented exemption.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-22
 **************************************************************************/

#ifndef KERNEL_DRIVERS_NVME_H_
#define KERNEL_DRIVERS_NVME_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Where an NVM Express controller sits on the bus
 * @{
 */
#define NON_VOLATILE_MEMORY_EXPRESS_CLASS     0x01u /**< Mass storage. */
#define NON_VOLATILE_MEMORY_EXPRESS_SUBCLASS  0x08u /**< Non-volatile memory. */
#define NON_VOLATILE_MEMORY_EXPRESS_INTERFACE 0x02u /**< NVM Express. */
/** @} */

/** Bytes kept of the serial number Identify Controller reports, its terminating NUL included. */
#define NON_VOLATILE_MEMORY_EXPRESS_SERIAL_NUMBER_CAPACITY 21u

/** Bytes kept of the model number Identify Controller reports, its terminating NUL included. */
#define NON_VOLATILE_MEMORY_EXPRESS_MODEL_NUMBER_CAPACITY 41u

/**
 * @brief How far the bring-up went, and what the controller said about itself.
 *
 * @details Each flag is set once its step succeeded, so a failed bring-up still says which step
 *          stopped it.
 */
typedef struct NonVolatileMemoryExpressState {
    bool controller_present;             /**< A controller of class 01h, subclass 08h, interface 02h is on the bus. */
    bool controller_ready;               /**< It came out of reset with the admin queues it was given. */
    bool identified;                     /**< It answered Identify for itself and for namespace 1. */
    const char *failure;                 /**< The step that failed, or NULL. */
    uint32_t version;                    /**< The version register: major in bits 31:16, minor in bits 15:8. */
    uint32_t maximum_queue_entries;      /**< Entries one queue may hold. */
    uint32_t doorbell_stride_bytes;      /**< Bytes between two doorbells. */
    uint32_t ready_timeout_milliseconds; /**< How long the controller may take to change its ready bit. */
    uint32_t namespace_count;            /**< Namespaces the controller supports. */
    uint64_t namespace_blocks;           /**< Blocks in namespace 1. */
    uint32_t block_bytes;                /**< Bytes in a block of namespace 1. */
    char serial_number[NON_VOLATILE_MEMORY_EXPRESS_SERIAL_NUMBER_CAPACITY]; /**< Without its padding spaces. */
    char model_number[NON_VOLATILE_MEMORY_EXPRESS_MODEL_NUMBER_CAPACITY];   /**< Without its padding spaces. */
} NonVolatileMemoryExpressState_t;

/**
 * @brief Brings the first NVM Express controller on the bus out of reset and identifies it.
 *
 * @details The controller is disabled, given an admin submission queue and an admin completion
 *          queue of one page each, enabled, then asked Identify Controller and Identify Namespace
 *          for namespace 1. Each wait is bounded by the timeout the controller reports, or by one
 *          second for a command.
 *
 * @return true when the controller and namespace 1 were identified; the state says how far the
 *         bring-up went otherwise.
 */
extern bool non_volatile_memory_express_initialize(void);

/**
 * @brief What the last non_volatile_memory_express_initialize found.
 *
 * @return The state; all zero before the first call.
 */
extern const NonVolatileMemoryExpressState_t *non_volatile_memory_express_state(void);

#ifdef __cplusplus
}
#endif

#endif /* !KERNEL_DRIVERS_NVME_H_ */
