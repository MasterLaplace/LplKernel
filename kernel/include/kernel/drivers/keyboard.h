/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** keyboard
*/

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
 * @brief Return number of raw scan codes dropped because the SPSC ring was full.
 */
extern uint32_t keyboard_get_dropped_char_count(void);

#endif /* KERNEL_DRIVERS_KEYBOARD_H */
