/**
 * @file ps2_mouse.h
 * @brief PS/2 auxiliary pointing device: packet decoding and a motion ring.
 *
 * The other half of the input story. The keyboard driver has been able to say
 * "the walker pressed W" since the beginning; nothing could say "the walker
 * LOOKED left", so the first-person camera turned in fixed steps from arrow keys
 * and the world could only be examined in increments.
 *
 * Same shape as the keyboard driver, for the same reasons: the interrupt handler
 * reads one byte, pushes it, and leaves. Assembling three bytes into a packet is
 * a state machine with state, and state assembled in interrupt context races with
 * whoever reads it — so assembly happens on the consumer side, in the main loop,
 * exactly where scancode decoding happens.
 *
 * Deltas rather than a position: a PS/2 mouse reports movement, not coordinates.
 * Integrating them into a cursor is a decision about a screen, and this layer has
 * no screen; a camera wants the deltas raw, and a pointer wants them accumulated.
 * The caller chooses.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @date 2026-07-31
 * @copyright MIT License
 */

#ifndef KERNEL_DRIVERS_PS2_MOUSE_H
#define KERNEL_DRIVERS_PS2_MOUSE_H

#include <stdint.h>

/** @brief One assembled movement report. */
typedef struct PersonalSystem2MousePacket_s {
    int32_t delta_x;         /**< Right is positive. */
    int32_t delta_y;         /**< UP is positive, as the device reports it. */
    uint8_t button_left;     /**< 1 while held. */
    uint8_t button_right;    /**< 1 while held. */
    uint8_t button_middle;   /**< 1 while held. */
} PersonalSystem2MousePacket_t;

/**
 * @brief Probes for an auxiliary device, enables it and hooks IRQ12.
 *
 * Safe to call on a machine with no mouse: every controller exchange is bounded
 * by a spin budget, so a port that never answers costs a fixed number of reads
 * and reports failure instead of hanging the boot. That is not hypothetical
 * caution — the smoke battery boots headless under QEMU with and without `-device
 * ...mouse`, and a blocking wait here would have hung one of the two.
 *
 * @return 1 if a device answered and data reporting was enabled, 0 otherwise.
 */
uint8_t personal_system_2_mouse_initialize(void);

/**
 * @brief Whether initialization found a device.
 * @return 1 if a device was found, 0 otherwise.
 */
uint8_t personal_system_2_mouse_is_present(void);

/**
 * @brief Pops one assembled movement report.
 * @return 1 if a packet was produced, 0 if the ring holds no complete packet.
 */
uint8_t personal_system_2_mouse_try_pop_packet(PersonalSystem2MousePacket_t *out_packet);

/**
 * @brief Raw bytes waiting to be assembled.
 * @return The number of raw bytes waiting to be assembled.
 */
uint32_t personal_system_2_mouse_get_pending_byte_count(void);

/**
 * @brief Interrupts serviced since boot.
 * @return The number of interrupts serviced since boot.
 */
uint32_t personal_system_2_mouse_get_irq_count(void);

/**
 * @brief Bytes dropped because the ring was full.
 * @return The number of bytes dropped because the ring was full.
 */
uint32_t personal_system_2_mouse_get_dropped_byte_count(void);

/**
 * @brief Packets discarded because the synchronisation bit was clear.
 *
 * A PS/2 stream has no framing beyond bit 3 of the first byte always being set.
 * Lose a byte and every packet after it is assembled from the wrong three, which
 * reads as the pointer flying off. Re-synchronising on that bit is the standard
 * repair, and this counter is how you find out it is happening at all.
 *
 * @return The number of packets discarded because the synchronisation bit was clear.
 */
uint32_t personal_system_2_mouse_get_resynchronization_count(void);

#endif /* KERNEL_DRIVERS_PS2_MOUSE_H */
