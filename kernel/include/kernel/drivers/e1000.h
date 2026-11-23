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
 * one buffer for its whole life, so nothing is allocated or copied per frame.
 *
 * Each ring is also a kernel_spsc_ring (kernel/memory/spsc_ring.h) whose slots are its descriptors, and
 * the driver plays the controller's end of it:
 *
 * - **receiving**, e1000_receive_harvest gives the controller back the slots the consumer released, with
 *   one write of the tail register, then publishes the slots whose DD bit the controller set. The caller
 *   is the ring's consumer: kernel_spsc_ring_peek hands out a run of slots, e1000_receive_bytes and
 *   e1000_receive_length read a frame where the controller wrote it, kernel_spsc_ring_release gives the
 *   run back;
 * - **sending**, the caller is the ring's producer: kernel_spsc_ring_reserve hands out a run of slots,
 *   e1000_transmit_buffer gives each one's buffer to write a frame into, e1000_transmit_describe writes its
 *   descriptor, and kernel_spsc_ring_publish publishes them. e1000_transmit_kick hands what was published to the
 *controller, with one write of the tail register and a report asked for its last frame only (RS), and gives back the
 *   slots of the frames the controller reported sent.
 *
 * The consumer of the frames received and the producer of the frames sent may run on another core than
 * the harvest and the kick, as on any kernel_spsc_ring, and sleep on the ring's write index. Everything
 * is polled: the controller's interrupts are masked and its pin interrupt is off.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-21
 **************************************************************************/

#ifndef KERNEL_DRIVERS_E1000_H_
#define KERNEL_DRIVERS_E1000_H_

#include <kernel/memory/spsc_ring.h>
#include <kernel/net/device.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name The rings and their buffers
 * @{
 */
#define E1000_RECEIVE_DESCRIPTORS  64u   /**< Receive slots: the controller fills at most one fewer at once. */
#define E1000_TRANSMIT_DESCRIPTORS 64u   /**< Transmit slots, all usable: the controller holds one fewer at once. */
#define E1000_BUFFER_BYTES         2048u /**< Bytes of each frame buffer, receive and transmit (RCTL.BSIZE 00b). */

#define E1000_RECEIVE_MOST_HELD (E1000_RECEIVE_DESCRIPTORS - 1u) /**< Slots it fills unreleased (§3.2.6). */
/** @} */

/**
 * @name Frames
 * @{
 */
#define E1000_ADDRESS_BYTES         6u  /**< Bytes of a station address. */
#define E1000_STATION_ADDRESS_WORDS 3u  /**< Words 00h to 02h of the controller's EEPROM hold its address (§5.6.1). */
#define E1000_FRAME_LEAST_BYTES     14u /**< Shortest frame a publish takes: one Ethernet header. */
#define E1000_FRAME_MOST_BYTES      1514u /**< Longest: a header and 1500 bytes; the controller adds the CRC. */
/** @} */

/**
 * @brief How far the bring-up went, and what the controller said about itself.
 *
 * @details Each flag is set once its step succeeded, so a failed bring-up still says which step stopped it.
 */
typedef struct E1000State {
    bool controller_present;    /**< An 8254x of a model this driver drives is on the bus. */
    bool controller_ready;      /**< It came out of reset with both rings, its receiver and its transmitter on. */
    bool link_up;               /**< STATUS.LU, as last read: at the end of the bring-up, or by e1000_link_up. */
    bool loopback;              /**< Its PHY's control register has the loopback bit set, as last read; false unread. */
    const char *failure;        /**< The step that failed, or NULL. */
    uint16_t device_identifier; /**< Its PCI device ID. */
    uint8_t address[E1000_ADDRESS_BYTES]; /**< Its station address, read from EEPROM words 0 to 2. */
} E1000State_t;

/**
 * @brief A legacy receive descriptor (317453-006EN, Table 3-1), as the controller writes it back.
 */
typedef struct E1000ReceiveDescriptor {
    uint64_t buffer_address;                 /**< Physical address of the slot's buffer. */
    volatile uint16_t length;                /**< Bytes the controller wrote, the CRC stripped. */
    volatile uint16_t checksum;              /**< Packet checksum. */
    volatile uint32_t status_errors_special; /**< STATUS in bits 7:0 (DD in bit 0, EOP in 1), ERRORS in 15:8. */
} E1000ReceiveDescriptor_t;

/**
 * @brief The frames received: a kernel_spsc_ring whose slots are the receive descriptors.
 *
 * @details e1000_receive_harvest is the ring's producer; the caller is its consumer.
 */
typedef struct E1000Receive {
    KernelSpscRing_t ring;                       /**< The ring; its consumer end is the caller's. */
    const E1000ReceiveDescriptor_t *descriptors; /**< Slot s's length, status and errors. */
    const uint8_t *buffers;                      /**< Slot s's frame starts at buffers + s * E1000_BUFFER_BYTES. */
} E1000Receive_t;

/**
 * @brief A legacy transmit descriptor (317453-006EN, Table 3-8).
 */
typedef struct E1000TransmitDescriptor {
    uint64_t buffer_address;              /**< Physical address of the slot's buffer. */
    uint32_t length_and_command;          /**< Length in bits 15:0, CSO in 23:16, CMD in 31:24. */
    volatile uint32_t status_and_special; /**< STA in bits 3:0 (DD in bit 0), written back only under RS. */
} E1000TransmitDescriptor_t;

/** CMD of a described frame, in its descriptor's third dword: EOP and IFCS, the controller adding the CRC. */
#define E1000_TRANSMIT_FRAME_COMMAND ((0x01u | 0x02u) << 24u)

/**
 * @brief The frames to send: a kernel_spsc_ring whose slots are the transmit descriptors.
 *
 * @details The caller is the ring's producer; e1000_transmit_kick is its consumer, for the controller.
 */
typedef struct E1000Transmit {
    KernelSpscRing_t ring;                  /**< The ring; its producer end is the caller's. */
    E1000TransmitDescriptor_t *descriptors; /**< Slot s's descriptor, which e1000_transmit_describe writes. */
    uint8_t *buffers;                       /**< Slot s's frame starts at buffers + s * E1000_BUFFER_BYTES. */
} E1000Transmit_t;

/**
 * @brief What went through the controller, as it counts it and as the driver counts it.
 *
 * @details The controller's counts are its statistics registers, which clear when read (317453-006EN,
 *          §13.7): each read is added here. QEMU 10.2 keeps MPC, RNBC and CRCERRS across reads (hw/net/e1000.c,
 *          macreg_readops), so under QEMU two reads that both find a missed frame add it twice.
 */
typedef struct E1000Counters {
    uint64_t frames_received;                /**< Frames the consumer released, given back to the controller. */
    uint64_t frames_sent;                    /**< Frames a kick handed to the controller. */
    uint64_t missed;                         /**< MPC: frames lost for want of room in the receive FIFO. */
    uint64_t no_buffers;                     /**< RNBC: frames that arrived while no receive descriptor was free. */
    uint64_t cyclic_redundancy_check_errors; /**< CRCERRS: frames received with a bad CRC. */
    uint64_t good_received;                  /**< GPRC: frames received sound. */
    uint64_t good_transmitted;               /**< GPTC: frames transmitted. */
    uint32_t register_reads;     /**< Reads of its registers since the bring-up, these counters' own included. */
    uint32_t register_writes;    /**< Writes of its registers since the bring-up. */
    uint32_t receive_doorbells;  /**< Writes of the receive tail since the bring-up. */
    uint32_t transmit_doorbells; /**< Writes of the transmit tail since the bring-up. */
    uint32_t receive_ring_full_harvests; /**< Harvests that left 63 frames waiting, all the controller can fill:
                                              frames arriving then are lost, which it counts in MPC or RNBC. */
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
 * @brief Decodes the station address from words 00h to 02h of the controller's EEPROM (317453-006EN, §5.6.1).
 *
 * @details Each word holds two bytes of the address, its first byte in the low half: the address
 *          12-34-56-78-90-AB is stored as 3412h, 7856h and AB90h.
 *
 * @param words   The E1000_STATION_ADDRESS_WORDS words, in address order.
 * @param address Receives the E1000_ADDRESS_BYTES bytes of the address.
 */
extern void e1000_station_address_from_words(const uint16_t *words, uint8_t *address);

/**
 * @brief Adds what one read of a statistics register gave to the counter it feeds (317453-006EN, §13.7).
 *
 * @details CRCERRS (4000h) feeds cyclic_redundancy_check_errors, MPC (4010h) missed, GPRC (4074h)
 *          good_received, GPTC (4080h) good_transmitted and RNBC (40A0h) no_buffers. The registers clear
 *          when read, so each read adds.
 *
 * @param counters        The counters.
 * @param register_offset The register's offset in the register window.
 * @param value           What the read gave.
 * @return false, with nothing added, for an offset that feeds none of the counters.
 */
extern bool e1000_add_statistics_register(E1000Counters_t *counters, uint32_t register_offset, uint32_t value);

/**
 * @brief Turns the PHY loopback on or off, the loop Linux's `ethtool -t` tests the controller through.
 *
 * @details On, the PHY is forced to 1000 Mb/s full duplex with its loopback bit set, and the MAC to the
 *          same speed, so frames sent come back to the receive ring without reaching the wire. Off,
 *          the loopback bit is cleared, the force bits too, and the PHY negotiates again.
 *
 * @param enabled Whether frames sent come back.
 * @return false when no controller is ready, the PHY did not take a write, or its loopback bit does not read
 *         back as asked.
 */
extern bool e1000_set_loopback(bool enabled);

/**
 * @brief Reads whether the link is up (STATUS.LU), into the state too.
 *
 * @details Leaving the loopback makes the PHY negotiate again, and the link stays down meanwhile.
 *
 * @return Whether the link is up; false when no controller is ready.
 */
extern bool e1000_link_up(void);

/**
 * @brief The frames received, whose ring's consumer end is the caller's.
 *
 * @return The receive side, never NULL; its ring stays empty while no controller is ready.
 */
extern E1000Receive_t *e1000_receive(void);

/**
 * @brief Gives the controller back the slots the consumer released, then publishes the frames it wrote.
 *
 * @details The slots released since the last harvest go back to the controller with one write of the receive
 *          tail; then every slot whose DD bit is set, oldest first, is published, at most the 63 the controller
 *          can fill, its DD bit cleared as it is read, so that no later lap reads it stale. Reads no register.
 *
 * @return Frames published by this harvest; 0 when none arrived or no controller is ready.
 */
extern uint32_t e1000_receive_harvest(void);

/**
 * @brief The first byte of the frame in receive slot @p slot, where the controller wrote it.
 *
 * @param receive The receive side.
 * @param slot    A slot kernel_spsc_ring_peek handed out.
 * @return The frame's first byte, valid until the slot is released.
 */
static inline const uint8_t *e1000_receive_bytes(const E1000Receive_t *receive, uint32_t slot);

/**
 * @brief The bytes of the frame in receive slot @p slot, the CRC stripped.
 *
 * @param receive The receive side.
 * @param slot    A slot kernel_spsc_ring_peek handed out.
 * @return Its length.
 */
static inline uint16_t e1000_receive_length(const E1000Receive_t *receive, uint32_t slot);

/**
 * @brief The status byte of receive slot @p slot: EOP in bit 1; DD, in bit 0, the harvest took.
 *
 * @param receive The receive side.
 * @param slot    A slot kernel_spsc_ring_peek handed out.
 * @return The byte.
 */
static inline uint8_t e1000_receive_status(const E1000Receive_t *receive, uint32_t slot);

/**
 * @brief The errors byte of receive slot @p slot: 0 for a frame the controller found sound.
 *
 * @param receive The receive side.
 * @param slot    A slot kernel_spsc_ring_peek handed out.
 * @return The byte.
 */
static inline uint8_t e1000_receive_errors(const E1000Receive_t *receive, uint32_t slot);

/**
 * @brief The frames to send, whose ring's producer end is the caller's.
 *
 * @return The transmit side, never NULL; kernel_spsc_ring_reserve on its ring hands out slots, ready or not.
 */
extern E1000Transmit_t *e1000_transmit(void);

/**
 * @brief The buffer of transmit slot @p slot, to write a frame into.
 *
 * @param transmit The transmit side.
 * @param slot     A slot kernel_spsc_ring_reserve handed out.
 * @return Its first byte; E1000_BUFFER_BYTES follow.
 */
static inline uint8_t *e1000_transmit_buffer(const E1000Transmit_t *transmit, uint32_t slot);

/**
 * @brief Describes the frame of @p length bytes the caller wrote into transmit slot @p slot, for
 *        kernel_spsc_ring_publish to publish.
 *
 * @details One store into the slot's descriptor, as the caller writes the frame; e1000_transmit_kick hands
 *          what was published to the controller.
 *
 * @param transmit The transmit side.
 * @param slot     A slot kernel_spsc_ring_reserve handed out.
 * @param length   The frame's bytes, from E1000_FRAME_LEAST_BYTES to E1000_FRAME_MOST_BYTES.
 * @return false, with the slot left as it was, for a length out of range: such a slot is not to be published.
 */
static inline bool e1000_transmit_describe(E1000Transmit_t *transmit, uint32_t slot, uint32_t length);

/**
 * @brief Gives back the slots of the frames the controller reported sent, then hands it what was
 *        published since, with one write of the transmit tail.
 *
 * @details A report is asked for the last frame handed (RS), which reports every frame before it, and only that
 *          descriptor's status is cleared: the controller writes no other back (§3.3.3). The controller is given
 *          at most 63 frames at once; a 64th published waits for the next kick.
 *
 * @return Frames handed to the controller by this kick; 0 when nothing new was published or no controller is ready.
 */
extern uint32_t e1000_transmit_kick(void);

/**
 * @brief The controller as a network device of the stack (kernel/net/device.h), its one queue pair the two rings.
 *
 * @return The device, or NULL while no controller is ready.
 */
extern KernelNetworkDevice_t *e1000_network_device(void);

/**
 * @brief Adds the controller's statistics registers to the counters, and returns them.
 *
 * @return The counters, never NULL.
 */
extern const E1000Counters_t *e1000_counters(void);

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
/**
 * @brief Holds the transmit tail back while @p held, so that a debug kernel's tests see a controller slower
 *        than the driver, which QEMU never is: it sends a kick's frames inside the write of its tail.
 *
 * @details While held, a kick hands its frames over and asks their report, but writes no tail; letting go
 *          writes the tail once, as one more transmit doorbell, when a kick was held.
 *
 * @param held Whether kicks leave the tail where the controller last read it.
 */
extern void e1000_debug_hold_transmit_tail(bool held);

/**
 * @brief Reads one register of the PHY, so that a debug kernel's tests see what the bring-up and the
 *        loopback left in it.
 *
 * @param physical_layer_register The register, 0 to 31.
 * @param out_value               Receives its value.
 * @return false when no controller is ready or the PHY did not answer.
 */
extern bool e1000_debug_read_physical_layer_register(uint32_t physical_layer_register, uint16_t *out_value);

/**
 * @brief Forgets what the PHY held before the loopback, so that a debug kernel's next bring-up meets a
 *        loopback it did not set, as after a warm reboot from a kernel that left the PHY in loopback.
 */
extern void e1000_debug_forget_loopback(void);
#endif

#include <kernel/drivers/e1000.inl>

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DRIVERS_E1000_H_ */
