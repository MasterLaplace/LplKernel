/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** PS/2 keyboard scan code decoder header
*/

#ifndef KERNEL_DRIVERS_PS2_KEYBOARD_H
#define KERNEL_DRIVERS_PS2_KEYBOARD_H

#include <stdint.h>

/**
 * @brief Selectable keyboard layout policy.
 *
 * The decoder maps physical PS/2 Set 1 scan codes to characters through the
 * table set selected here. New layouts are added by providing base/shift/altgr
 * tables in ps2_keyboard.c.
 */
typedef enum PersonalSystem2KeyboardLayout {
    PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_UNITED_STATES_QWERTY = 0u,
    PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_FRENCH_AZERTY = 1u,
} PersonalSystem2KeyboardLayout_t;

/**
 * @brief Decode a single PS/2 Set 1 scan code to a character.
 *
 * Set 1 encoding: a make code has the high bit clear, the matching break
 * (release) code is the same value with the high bit set (`code | 0x80`), and
 * extended keys are prefixed by a standalone `0xE0` byte.
 *
 * Handles:
 * - Printable keys through the active layout (respecting shift / caps lock).
 * - Modifier make AND break for left/right shift, left/right ctrl, left alt
 *   and right alt (AltGr), so modifiers never latch.
 * - Caps lock toggle (applied to alphabetic keys only).
 *
 * @note This function is stateful (it tracks modifiers) and is intended to be
 *       called from a single context (the bottom-half drain), never from the
 *       IRQ handler, to keep modifier state race-free.
 *
 * @param scancode The scan code byte read from the PS/2 controller.
 * @return Decoded character if a printable key was produced; 0x00 otherwise
 *         (modifiers, extended prefixes, key releases, unmapped keys).
 */
extern char personal_system_2_keyboard_decode_scancode(uint8_t scancode);

/**
 * @brief Select the active keyboard layout policy.
 * @param layout Layout to activate.
 */
extern void personal_system_2_keyboard_set_layout(PersonalSystem2KeyboardLayout_t layout);

/**
 * @brief Query the active keyboard layout policy.
 * @return The currently selected layout.
 */
extern PersonalSystem2KeyboardLayout_t personal_system_2_keyboard_get_layout(void);

/**
 * @brief Query modifier key state (shift key pressed).
 * @return Non-zero when either shift key is pressed.
 */
extern uint8_t personal_system_2_keyboard_get_shift_state(void);

/**
 * @brief Query modifier key state (ctrl key pressed).
 * @return Non-zero when either ctrl key is pressed.
 */
extern uint8_t personal_system_2_keyboard_get_ctrl_state(void);

/**
 * @brief Query modifier key state (alt key pressed).
 * @return Non-zero when either alt key is pressed.
 */
extern uint8_t personal_system_2_keyboard_get_alt_state(void);

/**
 * @brief Query caps lock toggle state.
 * @return Non-zero when caps lock is active.
 */
extern uint8_t personal_system_2_keyboard_get_caps_lock_state(void);

/**
 * @brief Reset all modifier key states and escape sequences.
 *
 * Useful after a full keyboard reset or error recovery.
 */
extern void personal_system_2_keyboard_reset_modifiers(void);

#endif /* KERNEL_DRIVERS_PS2_KEYBOARD_H */
