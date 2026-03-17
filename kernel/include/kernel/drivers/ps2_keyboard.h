/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** PS/2 keyboard scan code decoder header
*/

#ifndef KERNEL_DRIVERS_PS2_KEYBOARD_H_
#define KERNEL_DRIVERS_PS2_KEYBOARD_H_

#include <stdint.h>

/**
 * @brief Decode a PS/2 Set 1 scan code to ASCII character.
 *
 * Handles:
 * - Standard printable keys (respecting shift/caps lock state)
 * - Modifier tracking (shift, ctrl, alt, caps lock)
 * - Extended key sequences (0xE0 prefix)
 * - Key release codes (0xF0 prefix)
 *
 * @param scancode The scan code byte from the PS/2 controller.
 * @return ASCII character if a printable key was decoded; 0x00 otherwise
 *         (for modifiers, extended prefixes, etc).
 */
extern char ps2_keyboard_decode_scancode(uint8_t scancode);

/**
 * @brief Query modifier key state (shift key pressed).
 * @return Non-zero when either shift key is pressed.
 */
extern uint8_t ps2_keyboard_get_shift_state(void);

/**
 * @brief Query modifier key state (ctrl key pressed).
 * @return Non-zero when either ctrl key is pressed.
 */
extern uint8_t ps2_keyboard_get_ctrl_state(void);

/**
 * @brief Query modifier key state (alt key pressed).
 * @return Non-zero when either alt key is pressed.
 */
extern uint8_t ps2_keyboard_get_alt_state(void);

/**
 * @brief Query caps lock toggle state.
 * @return Non-zero when caps lock is active.
 */
extern uint8_t ps2_keyboard_get_caps_lock_state(void);

/**
 * @brief Reset all modifier key states and escape sequences.
 *
 * Useful after a full keyboard reset or error recovery.
 */
extern void ps2_keyboard_reset_modifiers(void);

#endif /* !KERNEL_DRIVERS_PS2_KEYBOARD_H_ */
