/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** keyboard
*/

#ifndef KEYBOARD_H_
#define KEYBOARD_H_

#include <stdint.h>

/**
 * @brief Install minimal IRQ1 keyboard handler.
 *
 * The handler currently logs raw scan codes to COM1 and sends EOI.
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
 * @brief Return number of decoded chars currently queued.
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
 * @brief Return number of decoded chars dropped because queue was full.
 */
extern uint32_t keyboard_get_dropped_char_count(void);

#endif /* !KEYBOARD_H_ */
