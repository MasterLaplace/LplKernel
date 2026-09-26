/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file hda.h
 * @brief Intel High Definition Audio controller — capture and playback.
 *
 * The interface `hal_audio.c` drives. Everything here is spelled out in
 * `intel_high_definition_audio_*` per the naming convention; the file name keeps
 * the acronym, which is the documented exemption.
 *
 * The controller is found over the bus enumeration that already exists (class 0x04,
 * subclass 0x03), so no new probing mechanism is needed — the same table walk the
 * cartridge and the network card go through.
 *
 * The interrupt handler does what every other driver here does and nothing more:
 * move a buffer into a ring and acknowledge. Ring buffer descriptor lists are set
 * up once at init; a handler that allocated would be a handler that can fail while
 * the sovereign is mid-sentence.
 *
 * WHAT IS VERIFIED. Measured in QEMU with `-device intel-hda -device hda-duplex`, on
 * all five booted artifacts: the controller is found, comes out of reset, reports
 * version 1 with four input and four output stream descriptors, a codec announces
 * itself with vendor identifier 0x1AF40022, all twelve verbs of the walk and the stream
 * setup are answered, the input converter is found at node 4 behind pin 3, and the
 * stream runs and delivers buffers. The delivery rate cross-checks the format word
 * independently: the cyclic buffer is 2560 bytes, which is 80 ms of 16 kHz mono
 * 16-bit audio, and 320 ms of running produced three wraps plus a part-filled fourth.
 *
 * HOW THE COMMAND PATH WAS FIXED, because the shape of the bug is worth keeping. For
 * a while only the FIRST verb was ever answered and every one after it timed out. Two
 * hypotheses were reasoned out from the code and both were wrong — acknowledging
 * RIRBSTS (correct in itself, kept) and a ring-pointer misalignment (imaginary). The
 * third attempt was an instrument instead of a hypothesis: a probe that reads both
 * rings either side of a failing command. It answered immediately. CORBWP was 2, our
 * shadow agreed, the fetch engine was running, CORBSTS showed no error — and CORBRP
 * was frozen at 1. The controller was not failing to answer, it was refusing to
 * FETCH, and only one setting does that: RINTCNT, the response count a controller
 * insists on having acknowledged before it reads more. It was set to one, which for a
 * driver that polls and takes no interrupts is the worst possible value. The probe is
 * kept rather than removed — a timeout without register state is a fact with no
 * diagnosis attached, which is exactly how this managed to be wrong twice.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef KERNEL_DRIVERS_HDA_H
#define KERNEL_DRIVERS_HDA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Codec slots the controller can report on its serial bus. */
#define INTEL_HIGH_DEFINITION_AUDIO_MAX_CODECS 15u

/**
 * @struct IntelHighDefinitionAudioRingProbe_t
 * @brief The eight numbers that say who is waiting on whom.
 *
 * A command that got no answer is a fact with no diagnosis attached: it does not say
 * whether the controller never fetched the entry, fetched it and the codec stayed
 * silent, or answered into a ring this driver was reading at the wrong offset. Those
 * are three different bugs with one symptom, and the command path here has already
 * been reasoned about wrongly twice — so the registers are captured either side of the
 * first failure instead.
 *
 * Both pointer pairs carry the driver's own shadow next to the hardware's value,
 * because a disagreement between the two is itself one of the three answers.
 */
typedef struct {
    uint16_t command_write_pointer_shadow; /**< What this driver believes it wrote. */
    uint16_t command_write_pointer;        /**< CORBWP, as the controller reports it. */
    uint16_t command_read_pointer;         /**< CORBRP: how far the controller has fetched. */
    uint16_t response_write_pointer;       /**< RIRBWP: how far it has answered. */
    uint16_t response_read_pointer_shadow; /**< How far this driver has consumed. */
    uint8_t command_ring_control;          /**< CORBCTL: is the fetch engine running. */
    uint8_t command_ring_status;           /**< CORBSTS: bit 0 is a memory error. */
    uint8_t response_ring_control;         /**< RIRBCTL: is the response engine running. */
    uint8_t response_ring_status;          /**< RIRBSTS: response flag and overrun. */
} IntelHighDefinitionAudioRingProbe_t;

/**
 * @struct IntelHighDefinitionAudioState_t
 * @brief What the bring-up found.
 *
 * Reported field by field rather than summarised into a boolean, because the
 * interesting failures are partial: a controller that resets but whose codec never
 * announces itself, or a codec that answers its vendor identifier and then has no
 * input converter, are different problems with the same one-word answer.
 */
typedef struct {
    bool controller_present;                                       /**< A device of the right class is on the bus. */
    bool controller_running;                                       /**< It came out of reset. */
    bool rings_running;                                            /**< The command and response rings are live. */
    uint32_t bar_virtual;                                          /**< Where the register window was mapped. */
    uint8_t major_version;                                         /**< Specification major version it reports. */
    uint8_t minor_version;                                         /**< Specification minor version. */
    uint8_t output_streams;                                        /**< Output stream descriptors it implements. */
    uint8_t input_streams;                                         /**< Input stream descriptors it implements. */
    uint16_t codec_mask;                                           /**< Bit per codec slot that answered the reset. */
    uint32_t codec_vendor[INTEL_HIGH_DEFINITION_AUDIO_MAX_CODECS]; /**< Vendor identifier per slot. */
    uint32_t verbs_sent;                                           /**< Commands written to the ring. */
    uint32_t responses_read;                                       /**< Responses read back. */
    uint32_t verb_timeouts;                                        /**< Commands that never got an answer. */
    uint8_t audio_function_group;                                  /**< Node identifier of the audio function group. */
    uint8_t capture_converter;  /**< Input converter widget, 0 when none was found. */
    uint8_t capture_pin;        /**< Input-capable pin feeding it, 0 when none was found. */
    uint8_t playback_converter; /**< Output converter widget, 0 when none was found. */
    uint8_t playback_pin;       /**< Output-capable pin, 0 when none was found. */
    uint8_t widgets_walked;     /**< Widgets examined while looking for them. */
    /**
     * Output amplifiers this driver muted at bring-up, and never unmutes.
     *
     * Counted rather than assumed: a mute verb that silently failed to send would be
     * indistinguishable from one that worked, and the whole point of muting here is
     * that a development machine cannot be made to emit noise by a kernel under test.
     */
    uint8_t outputs_muted;
    bool capture_running;                             /**< The input stream descriptor is running. */
    uint32_t capture_position;                        /**< Link position, in bytes, at the last poll. */
    uint32_t capture_wraps;                           /**< Times the cyclic buffer came back round. */
    bool probe_captured;                              /**< A command timed out and its registers were kept. */
    uint32_t probe_command;                           /**< The command word that got no answer. */
    IntelHighDefinitionAudioRingProbe_t probe_before; /**< Rings before it was submitted. */
    IntelHighDefinitionAudioRingProbe_t probe_after;  /**< Rings after the budget ran out. */
    uint8_t interrupt_line;                           /**< PCI configuration offset 0x3C, or KERNEL_HDA_NO_INTERRUPT_LINE. */
} IntelHighDefinitionAudioState_t;

/**
 * No interrupt line known. 0xFF is what the PCI specification writes for "not
 * connected", and it keeps the sentinel off every real line: zero, the value a cleared
 * state would otherwise hold, is the timer's.
 */
#define KERNEL_HDA_NO_INTERRUPT_LINE 0xFFu

/**
 * @brief Finds the controller, resets it and brings its command rings up.
 *
 * @param out Receives what was found; may be NULL.
 * @return true when a codec answered.
 */
bool intel_high_definition_audio_initialize(IntelHighDefinitionAudioState_t *out);

/**
 * @brief The state of the last bring-up.
 * @return The state; every field is zero before @ref intel_high_definition_audio_initialize.
 */
const IntelHighDefinitionAudioState_t *intel_high_definition_audio_state(void);

/**
 * @brief Sends one command to a codec and waits for its answer.
 *
 * Synchronous, and bounded: a codec that does not answer within the spin budget is
 * counted as a timeout rather than hanging the boot. A controller in a state nobody
 * anticipated is a thing to report, not a thing to wait for forever.
 *
 * @param codec       Slot, below @ref INTEL_HIGH_DEFINITION_AUDIO_MAX_CODECS.
 * @param node        Node identifier inside the codec; 0 is the root.
 * @param verb        Twelve-bit verb.
 * @param payload     Eight-bit payload.
 * @param out_response Receives the answer.
 * @return true when an answer came back.
 */
bool intel_high_definition_audio_command(uint8_t codec, uint8_t node, uint16_t verb, uint8_t payload,
                                         uint32_t *out_response);

/**
 * @brief Sends one command whose verb is four bits and whose payload is sixteen.
 *
 * The other half of the verb encoding, and it needs its own entry point rather than a
 * wider payload argument: the two forms put the verb in DIFFERENT bit positions, so a
 * single function taking a sixteen-bit payload would silently truncate one of them.
 *
 * @note The four-bit form puts the verb at bits 19:16 and the payload at 15:0; the
 *       twelve-bit form of @ref intel_high_definition_audio_command puts the verb at 19:8
 *       and the payload at 7:0. Same word, two layouts.
 *
 * @param codec        Slot.
 * @param node         Node identifier.
 * @param verb         Four-bit verb.
 * @param payload      Sixteen-bit payload.
 * @param out_response Receives the answer.
 * @return true when an answer came back.
 */
bool intel_high_definition_audio_command_wide(uint8_t codec, uint8_t node, uint8_t verb, uint16_t payload,
                                              uint32_t *out_response);

/**
 * @brief Starts capturing into a cyclic buffer.
 *
 * Sixteen kilohertz, sixteen bits, one channel — the satellite's format, chosen there
 * rather than here and passed down so there is one statement of it.
 *
 * @return true when the stream descriptor is running.
 */
bool intel_high_definition_audio_start_capture(void);

/**
 * @brief Moves whatever the controller has captured into the caller's buffer.
 *
 * Polled rather than interrupt-driven, and that is a deliberate first step: an
 * interrupt handler that pushed into the ring would be the right shape, and it would
 * also be the second thing that could be wrong while the first is still unproven.
 * What this does is read the link position, notice when a buffer half has been
 * completed, and hand that half over.
 *
 * @note Only the half the controller is NOT writing is read: that one is complete and
 *       safe to copy, which is the entire reason the buffer has two.
 *
 * @param out      Receives samples.
 * @param capacity Room in @p out, in samples.
 * @return Samples written; 0 when the controller has not finished a half yet.
 */
uint32_t intel_high_definition_audio_poll_capture(int16_t *out, uint32_t capacity);

/**
 * @brief The legacy interrupt line the firmware assigned to the controller.
 * @return The line, or @ref KERNEL_HDA_NO_INTERRUPT_LINE.
 */
uint8_t intel_high_definition_audio_capture_interrupt_line(void);

/**
 * @brief Asks the controller to interrupt each time a capture half completes.
 *
 * @details Sets interrupt-on-completion on the input stream and enables it, with the
 *          global bit, in INTCTL. The buffer descriptors already carry the completion
 *          flag, so this is the last switch. Call only once a handler is in place.
 *
 * @note The stream status is cleared first, so a completion that happened while polling
 *       cannot fire the instant the enable lands and be read as the first real one.
 *
 * @return false when no capture stream is running.
 */
bool intel_high_definition_audio_enable_capture_interrupt(void);

/**
 * @brief Clears whatever the capture stream asserted, and says whether a half completed.
 *
 * @details Every status bit seen is written back, errors included: the line is
 *          level-triggered, so a bit left set keeps it asserted and the handler would
 *          be re-entered for ever.
 *
 * @return true when a buffer half completed.
 */
bool intel_high_definition_audio_acknowledge_capture_interrupt(void);

/**
 * @brief Raw capture registers, for telling a controller that stopped raising from a
 *        line that stopped delivering.
 *
 * @param out_stream_status    SDnSTS of the capture stream.
 * @param out_interrupt_status INTSTS.
 * @param out_position         Link position in bytes.
 */
void intel_high_definition_audio_capture_registers(uint8_t *out_stream_status, uint32_t *out_interrupt_status,
                                                   uint32_t *out_position);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DRIVERS_HDA_H */
