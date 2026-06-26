/**
 * @file hal_input.c
 * @brief Input backend for the engine HAL.
 *
 * Implements the hal_input_* contract over the PS/2 keyboard's lock-free SPSC
 * ring (ISR producer -> engine consumer). The engine drains decoded characters;
 * the kernel keeps owning scancode decoding and layout state. This generalizes
 * to additional input devices behind the same drain contract.
 */
#include <kernel/hal/hal.h>

#include <kernel/drivers/keyboard.h>

#include <stddef.h>

bool hal_input_try_pop_character(char *out_character)
{
    if (out_character == NULL)
        return false;
    return keyboard_try_pop_char(out_character) != 0u;
}

uint32_t hal_input_pending_count(void) { return keyboard_get_pending_char_count(); }
