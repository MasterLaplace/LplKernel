/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** keyboard
*/

#ifndef KEYBOARD_H_
#define KEYBOARD_H_

/**
 * @brief Install minimal IRQ1 keyboard handler.
 *
 * The handler currently logs raw scan codes to COM1 and sends EOI.
 */
extern void keyboard_interrupt_initialize(void);

#endif /* !KEYBOARD_H_ */
