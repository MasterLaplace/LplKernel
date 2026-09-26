/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
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
 * @file keyboard.h
 * @brief Keyboard interrupt handler and its scancode ring.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-06
 **************************************************************************/

#ifndef KERNEL_DRIVERS_KEYBOARD_H
#define KERNEL_DRIVERS_KEYBOARD_H

#include <stdint.h>

/**
 * @brief Install the IRQ1 keyboard handler.
 *
 * The handler is intentionally minimal: it reads the raw scan code, pushes it
 * onto a lock-free SPSC ring and sends EOI. Decoding happens later on the
 * consumer side (see keyboard_try_pop_char), keeping interrupt latency bounded.
 */
extern void keyboard_interrupt_initialize(void);

/**
 * @brief Return total number of IRQ1 keyboard interrupts handled.
 */
extern uint32_t keyboard_get_irq_count(void);

/**
 * @brief Return count of decoded printable characters seen on IRQ1.
 */
extern uint32_t keyboard_get_printable_count(void);

/**
 * @brief Return last decoded printable character, or 0 when none.
 */
extern char keyboard_get_last_printable_char(void);

/**
 * @brief Return number of raw scan codes pending decode in the SPSC ring.
 */
extern uint32_t keyboard_get_pending_char_count(void);

/**
 * @brief Pop one decoded char from keyboard queue.
 *
 * @param out_char Destination for popped character.
 * @return Non-zero if one character was popped; zero when queue empty.
 */
extern uint8_t keyboard_try_pop_char(char *out_char);

/**
 * @brief Return the number of scan codes the interrupt-side ring holds before it refuses.
 *
 * @details Read from the driver rather than restated by the caller: a capacity
 *          declared twice is a capacity that can disagree with itself.
 *
 * @return The ring capacity in scan codes.
 */
extern uint32_t keyboard_get_ring_capacity(void);

/**
 * @brief The scan-code ring's write index.
 *
 * @details Returned as an address so a caller can arm a monitor on it and sleep until a
 *          key arrives. The producer is the IRQ1 handler, which is what makes this a
 *          usable watch: an index only the waiting thread advances is a wait that cannot
 *          end. Volatile for the same reason.
 *
 * @return The address. Never NULL.
 */
extern const volatile uint32_t *keyboard_get_ring_head_address(void);

/**
 * @brief Return number of raw scan codes dropped because the SPSC ring was full.
 */
extern uint32_t keyboard_get_dropped_char_count(void);

#endif /* KERNEL_DRIVERS_KEYBOARD_H */
