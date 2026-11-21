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
 * @file e1000.h
 * @brief Intel 8254x Gigabit Ethernet controller: found on the bus, reset, and its frames moved in place.
 *
 * The 82540EM is the network card QEMU's `pc` machine attaches by default (`-device e1000`), and the
 * one VirtualBox emulates; the 82545EM is VMware's. The controller and the driver share two rings of
 * legacy descriptors in pinned memory (Intel 317453-006EN, §3.2 and §3.3), and each descriptor keeps
 * one buffer for its whole life, so nothing is allocated or copied per frame:
 *
 * - **receiving**, the controller writes a frame into a buffer and sets the descriptor's DD bit. A
 *   peek reads DD in memory, never a register, and hands out the frames where they lie; a release
 *   gives them back with one write of the tail register, whatever their count;
 * - **sending**, a reserve hands out free buffers, the caller writes its frames into them, and a
 *   publish describes them with one write of the tail register. Only the last descriptor of a
 *   publish asks the controller to report it done (RS), which reclaims the whole publish.
 *
 * Everything is polled: the controller's interrupts are masked and its pin interrupt is off. One
 * core drives it, so the peek and release side is single-consumer and the reserve and publish side
 * single-producer.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-21
 **************************************************************************/

#ifndef KERNEL_DRIVERS_E1000_H_
#define KERNEL_DRIVERS_E1000_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name The rings and their buffers
 * @{
 */
#define E1000_RECEIVE_DESCRIPTORS  64u   /**< Receive descriptors: the controller holds at most one fewer frames. */
#define E1000_TRANSMIT_DESCRIPTORS 64u   /**< Transmit descriptors: at most one fewer frames are in flight. */
#define E1000_BUFFER_BYTES         2048u /**< Bytes of each frame buffer, receive and transmit (RCTL.BSIZE 00b). */
/** @} */

/**
 * @name Frames
 * @{
 */
#define E1000_ADDRESS_BYTES        6u    /**< Bytes of a station address. */
#define E1000_EEPROM_ADDRESS_WORDS 3u    /**< EEPROM words 00h to 02h, which hold the station address (§5.6.1). */
#define E1000_FRAME_LEAST_BYTES    14u   /**< Shortest frame a publish takes: one Ethernet header. */
#define E1000_FRAME_MOST_BYTES     1514u /**< Longest: a header and 1500 bytes of payload; the controller adds the CRC. */
/** @} */

/**
 * @brief How far the bring-up went, and what the controller said about itself.
 *
 * @details Each flag is set once its step succeeded, so a failed bring-up still says which step stopped it.
 */
typedef struct E1000State {
    bool controller_present;    /**< An 8254x of a model this driver drives is on the bus. */
    bool controller_ready;      /**< It came out of reset with both rings, its receiver and its transmitter on. */
    bool link_up;               /**< STATUS.LU when the bring-up ended. */
    bool loopback;              /**< Its PHY's control register has the loopback bit set, as last read. */
    const char *failure;        /**< The step that failed, or NULL. */
    uint16_t device_identifier; /**< Its PCI device ID. */
    uint8_t address[E1000_ADDRESS_BYTES]; /**< Its station address, read from EEPROM words 0 to 2. */
    const uint8_t *receive_buffers;       /**< Its first receive buffer: received frames lie in the next bytes. */
    uint32_t receive_buffers_bytes;       /**< Bytes of all its receive buffers. */
} E1000State_t;

/**
 * @brief A received frame, where the controller wrote it.
 */
typedef struct E1000ReceivedFrame {
    const uint8_t *bytes; /**< Its first byte, in the driver's receive buffer, until it is released. */
    uint16_t length;      /**< Its bytes, the CRC stripped. */
    uint8_t status;       /**< The descriptor's status byte: DD in bit 0, EOP in bit 1. */
    uint8_t errors;       /**< The descriptor's errors byte: 0 for a frame the controller found sound. */
} E1000ReceivedFrame_t;

/**
 * @brief What went through the controller, as it counts it and as the driver counts it.
 *
 * @details The controller's counts are its statistics registers, which clear when read (317453-006EN,
 *          §13.7): each read is added here. QEMU 10.2 keeps MPC and RNBC across reads (hw/net/e1000.c,
 *          macreg_readops), so under QEMU two reads that both find a missed frame add it twice.
 */
typedef struct E1000Counters {
    uint64_t frames_received;                /**< Frames released after a peek. */
    uint64_t frames_sent;                    /**< Frames published. */
    uint64_t missed;                         /**< MPC: frames lost for want of room in the receive FIFO. */
    uint64_t no_buffers;                     /**< RNBC: frames that arrived while no receive descriptor was free. */
    uint64_t cyclic_redundancy_check_errors; /**< CRCERRS: frames received with a bad CRC. */
    uint64_t good_received;                  /**< GPRC: frames received sound. */
    uint64_t good_transmitted;               /**< GPTC: frames transmitted. */
    uint32_t register_reads;     /**< Reads of its registers since the bring-up, these counters' own included. */
    uint32_t register_writes;    /**< Writes of its registers since the bring-up. */
    uint32_t receive_doorbells;  /**< Writes of the receive tail since the bring-up. */
    uint32_t transmit_doorbells; /**< Writes of the transmit tail since the bring-up. */
    uint32_t refused;            /**< Releases and publishes refused since the bring-up. */
} E1000Counters_t;

/**
 * @brief Brings the first 8254x on the bus out of reset, with both rings and every interrupt masked.
 *
 * @details The controller is reset, its station address read from its EEPROM and written to receive
 *          address 0, its statistics cleared, and its receiver and transmitter enabled with every
 *          receive descriptor but one given to it. A second call starts both rings over, and drops
 *          the frames that were not released.
 *
 * @return true when the controller is ready; the state says how far the bring-up went otherwise.
 */
extern bool e1000_initialize(void);

/**
 * @brief The state the last bring-up left.
 *
 * @return The state, never NULL.
 */
extern const E1000State_t *e1000_state(void);

/**
 * @brief Decodes the station address from EEPROM words 00h to 02h (317453-006EN, §5.6.1).
 *
 * @details Each word holds two bytes of the address, its first byte in the low half: the address
 *          12-34-56-78-90-AB is stored as 3412h, 7856h and AB90h.
 *
 * @param words   The E1000_EEPROM_ADDRESS_WORDS words, in address order.
 * @param address Receives the E1000_ADDRESS_BYTES bytes of the address.
 */
extern void e1000_station_address_from_eeprom(const uint16_t *words, uint8_t *address);

/**
 * @brief Decodes the status and errors bytes of a receive descriptor's last dword (317453-006EN, Table 3-1).
 *
 * @param status_errors_special The dword: STATUS in bits 7:0, ERRORS in 15:8, special in 31:16.
 * @param frame                 Receives the status and errors bytes; its other fields are left as they were.
 */
extern void e1000_decode_receive_status(uint32_t status_errors_special, E1000ReceivedFrame_t *frame);

/**
 * @brief Turns the PHY loopback on or off, the loop Linux's `ethtool -t` tests the controller through.
 *
 * @details On, the PHY is forced to 1000 Mb/s full duplex with its loopback bit set, and the MAC to the
 *          same speed, so frames sent come back to the receive ring without reaching the wire. Off,
 *          the loopback bit is cleared, the force bits too, and the PHY negotiates again.
 *
 * @param enabled Whether frames sent come back.
 * @return false when no controller is ready or the PHY did not take a write.
 */
extern bool e1000_set_loopback(bool enabled);

/**
 * @brief Hands out the frames received and not yet released, the oldest first, where they lie.
 *
 * @details Reads only the descriptors in memory. A second peek before a release hands out the same
 *          frames first.
 *
 * @param frames Receives up to @p wanted frames.
 * @param wanted Most frames wanted.
 * @return How many frames were written to @p frames; 0 when none arrived or no controller is ready.
 */
extern uint32_t e1000_receive_peek(E1000ReceivedFrame_t *frames, uint32_t wanted);

/**
 * @brief Gives the @p count oldest frames peeked back to the controller, with one write of its tail.
 *
 * @param count Frames released: 0 writes nothing.
 * @return false, with nothing released, when @p count is more than the frames peeked.
 */
extern bool e1000_receive_release(uint32_t count);

/**
 * @brief Hands out free transmit buffers, for the next publish to describe.
 *
 * @details Reclaims first the publishes the controller reported done. A reserve that hands out
 *          fewer than @p wanted says the ring is full.
 *
 * @param buffers Receives up to @p wanted buffers of E1000_BUFFER_BYTES, in the order a publish sends them.
 * @param wanted  Most buffers wanted.
 * @return How many buffers were written to @p buffers; 0 when none is free or no controller is ready.
 */
extern uint32_t e1000_transmit_reserve(uint8_t **buffers, uint32_t wanted);

/**
 * @brief Sends the frames written into the first @p count buffers of the last reserve, with one write of its tail.
 *
 * @details A publish that sends uses up the reserve, whether or not it sent every buffer the reserve
 *          handed out; a refused publish keeps it, for a publish that corrects the lengths.
 *
 * @param lengths Each frame's bytes, from E1000_FRAME_LEAST_BYTES to E1000_FRAME_MOST_BYTES.
 * @param count   Frames sent: 0 writes nothing.
 * @return false, with nothing sent, when @p count is more than the last reserve handed out or a length is
 *         out of range.
 */
extern bool e1000_transmit_publish(const uint16_t *lengths, uint32_t count);

/**
 * @brief Adds the controller's statistics registers to the counters, and returns them.
 *
 * @return The counters, never NULL.
 */
extern const E1000Counters_t *e1000_counters(void);

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
/**
 * @brief Holds the transmit tail back while @p held, so that a debug kernel's tests see a controller slower
 *        than the driver, which QEMU never is: it sends a publish inside the write of its tail.
 *
 * @details While held, a publish describes its frames and counts them, but writes no tail; letting go
 *          writes the tail once, as one more transmit doorbell, when a publish was held.
 *
 * @param held Whether publishes leave the tail where the controller last read it.
 */
extern void e1000_debug_hold_transmit_tail(bool held);
#endif

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DRIVERS_E1000_H_ */
