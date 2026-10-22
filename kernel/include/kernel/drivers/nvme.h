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
 * @brief NVM Express controller: found on the bus, reset, identified, and read and written.
 *
 * The controller is brought out of reset with its admin queue pair, answers Identify for itself
 * and for its first namespace, and takes one I/O queue pair through which namespace 1 is read and
 * written. Everything is polled; no interrupt is used. Everything here is spelled out in
 *`non_volatile_memory_express_*` per the naming convention; the file name keeps the acronym, which is the documented
 *exemption.
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
    bool input_output_queues_ready;      /**< It took an I/O submission queue and an I/O completion queue. */
    const char *failure;                 /**< The step that failed, or NULL. */
    uint32_t version;                    /**< The version register: major in bits 31:16, minor in bits 15:8. */
    uint32_t maximum_queue_entries;      /**< Entries one queue may hold. */
    uint32_t doorbell_stride_bytes;      /**< Bytes between two doorbells. */
    uint32_t ready_timeout_milliseconds; /**< How long the controller may take to change its ready bit. */
    uint32_t namespace_count;            /**< Namespaces the controller supports. */
    uint64_t namespace_blocks;           /**< Blocks in namespace 1. */
    uint32_t block_bytes;                /**< Bytes in a block of namespace 1. */
    uint32_t maximum_transfer_bytes;     /**< The most one read or write moves. */
    char serial_number[NON_VOLATILE_MEMORY_EXPRESS_SERIAL_NUMBER_CAPACITY]; /**< Without its padding spaces. */
    char model_number[NON_VOLATILE_MEMORY_EXPRESS_MODEL_NUMBER_CAPACITY];   /**< Without its padding spaces. */
} NonVolatileMemoryExpressState_t;

/**
 * @brief Brings the first NVM Express controller on the bus out of reset and identifies it.
 *
 * @details The controller is disabled, given an admin submission queue and an admin completion
 *          queue of one page each, enabled, then asked Identify Controller and Identify Namespace
 *          for namespace 1. It is then given one I/O completion queue and one I/O submission queue.
 *          Each wait is bounded by the timeout the controller reports, or by one second for a
 *          command.
 *
 * @return true when the controller and namespace 1 were identified and the I/O queues created;
 *         the state says how far the bring-up went otherwise.
 */
extern bool non_volatile_memory_express_initialize(void);

/**
 * @brief What the last non_volatile_memory_express_initialize found.
 *
 * @return The state; all zero before the first call.
 */
extern const NonVolatileMemoryExpressState_t *non_volatile_memory_express_state(void);

/**
 * @brief Reads @p block_count blocks of namespace 1, from @p first_block, into @p buffer.
 *
 * @details The buffer is described to the controller page by page, so its pages need not be
 *          contiguous in physical memory. The call returns once the controller has completed
 *          the read, or after one second.
 *
 * @param first_block The first block read.
 * @param block_count Blocks read, at least one; @p block_count blocks fit in the maximum transfer.
 * @param buffer      Mapped kernel memory of @p block_count blocks, aligned on 4 bytes.
 * @return true when every block was read; false when the bring-up did not create the I/O queues,
 *         when the range or the size is out of bounds, or when the controller reported an error.
 */
extern bool non_volatile_memory_express_read_blocks(uint64_t first_block, uint32_t block_count, void *buffer);

/**
 * @brief Writes @p block_count blocks of namespace 1, from @p first_block, out of @p buffer.
 *
 * @details The same rules as non_volatile_memory_express_read_blocks. A completed write is in the
 *          controller, not necessarily on the medium: no Flush is sent.
 *
 * @param first_block The first block written.
 * @param block_count Blocks written, at least one; @p block_count blocks fit in the maximum transfer.
 * @param buffer      Mapped kernel memory of @p block_count blocks, aligned on 4 bytes.
 * @return true when every block was written; false in the same cases as a read.
 */
extern bool non_volatile_memory_express_write_blocks(uint64_t first_block, uint32_t block_count, const void *buffer);

#ifdef __cplusplus
}
#endif

#endif /* !KERNEL_DRIVERS_NVME_H_ */
