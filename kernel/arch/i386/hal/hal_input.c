/**
 * @file hal_input.c
 * @brief Input backend for the engine HAL.
 *
 * Implements the hardware_abstraction_layer_input_* contract over the PS/2 keyboard's lock-free SPSC
 * ring (ISR producer -> engine consumer). The engine drains decoded characters;
 * the kernel keeps owning scancode decoding and layout state. This generalizes
 * to additional input devices behind the same drain contract.
 */
#include <kernel/hal/hal.h>

#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/ps2_keyboard.h>
#include <kernel/drivers/ps2_mouse.h>

#include <stddef.h>

bool hardware_abstraction_layer_input_try_pop_character(char *out_character)
{
    if (out_character == NULL)
        return false;
    return keyboard_try_pop_char(out_character) != 0u;
}

uint32_t hardware_abstraction_layer_input_pending_count(void) { return keyboard_get_pending_char_count(); }

bool hardware_abstraction_layer_input_try_pop_pointer(int32_t *out_delta_x, int32_t *out_delta_y, uint32_t *out_buttons)
{
    PersonalSystem2MousePacket_t packet;

    if (out_delta_x == NULL || out_delta_y == NULL || out_buttons == NULL)
        return false;
    if (!personal_system_2_mouse_try_pop_packet(&packet))
        return false;

    *out_delta_x = packet.delta_x;
    *out_delta_y = packet.delta_y;
    *out_buttons =
        (uint32_t) packet.button_left | ((uint32_t) packet.button_right << 1) | ((uint32_t) packet.button_middle << 2);
    return true;
}

bool hardware_abstraction_layer_input_pointer_available(void) { return personal_system_2_mouse_is_present() != 0u; }

bool hardware_abstraction_layer_input_is_key_held(char character)
{
    return personal_system_2_keyboard_is_character_held(character) != 0u;
}

uint32_t hardware_abstraction_layer_input_pointer_interrupt_count(void)
{
    return personal_system_2_mouse_get_irq_count();
}

uint32_t hardware_abstraction_layer_input_pointer_resynchronization_count(void)
{
    return personal_system_2_mouse_get_resynchronization_count();
}
