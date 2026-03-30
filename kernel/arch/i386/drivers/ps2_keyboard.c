/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** PS/2 keyboard scan code decoder
*/

#include <kernel/drivers/ps2_keyboard.h>
#include <stdbool.h>
#include <stdint.h>

/* PS/2 Set 1 scan code table for standard US layout */
static const char ps2_scancode_table[128] = {
    0x00, '\033', '1', '2', '3', '4', '5', '6', '7', '8',  /* 0x00-0x09 */
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',    /* 0x0A-0x13 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0x00,    /* 0x14-0x1D */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',      /* 0x1E-0x27 */
    '\'', '`', 0x00, '\\', 'z', 'x', 'c', 'v', 'b', 'n',   /* 0x28-0x31 */
    'm', ',', '.', '/', 0x00, '*', 0x00, ' ', 0x00, 0x00,  /* 0x32-0x3B */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x3C-0x43 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x44-0x4B */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x4C-0x53 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x54-0x5B */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x5C-0x63 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x64-0x6B */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x6C-0x73 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x74-0x7B */
};

/* Uppercase variant for when shift/caps is active */
static const char ps2_scancode_table_shift[128] = {
    0x00, '\033', '!', '@', '#', '$', '%', '^', '&', '*',  /* 0x00-0x09 */
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R',    /* 0x0A-0x13 */
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0x00,    /* 0x14-0x1D */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',      /* 0x1E-0x27 */
    '\"', '~', 0x00, '|', 'Z', 'X', 'C', 'V', 'B', 'N',    /* 0x28-0x31 */
    'M', '<', '>', '?', 0x00, '*', 0x00, ' ', 0x00, 0x00,  /* 0x32-0x3B */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x3C-0x43 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x44-0x4B */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x4C-0x53 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x54-0x5B */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x5C-0x63 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x64-0x6B */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x6C-0x73 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        /* 0x74-0x7B */
};

/* Modifier key tracking */
static uint8_t ps2_shift_state = 0u;      /* left/right shift (0x2A/0x36) */
static uint8_t ps2_ctrl_state = 0u;       /* left/right ctrl (0x1D/0xE0 0x1D) */
static uint8_t ps2_alt_state = 0u;        /* left/right alt (0x38/0xE0 0x38) */
static uint8_t ps2_caps_lock_state = 0u;  /* caps lock toggle */
static uint8_t ps2_escape_sequence = 0u;  /* expect 0xE0 prefix */

char ps2_keyboard_decode_scancode(uint8_t scancode)
{
    if (scancode == 0xE0)
    {
        ps2_escape_sequence = 1u;
        return 0x00;
    }

    if (scancode == 0xF0)
        return 0x00;

    if (ps2_escape_sequence && (scancode & 0x80))
    {
        ps2_escape_sequence = 0u;
        uint8_t code = scancode & 0x7F;

        if (code == 0x14)
            ps2_ctrl_state = 0u;
        else if (code == 0x11)
            ps2_alt_state = 0u;

        return 0x00;
    }

    if (scancode == 0x80)
        return 0x00;

    if (scancode == 0x2A)
    {
        ps2_shift_state = 1u;
        return 0x00;
    }
    if (scancode == 0x36)
    {
        ps2_shift_state = 1u;
        return 0x00;
    }
    if (scancode == 0x1D)
    {
        if (ps2_escape_sequence)
            ps2_escape_sequence = 0u;
        else
            ps2_ctrl_state = 1u;
        return 0x00;
    }
    if (scancode == 0x38)
    {
        if (ps2_escape_sequence)
        {
            ps2_escape_sequence = 0u;
            ps2_alt_state = 1u;
        }
        return 0x00;
    }
    if (scancode == 0x3A)
    {
        ps2_caps_lock_state ^= 1u;
        return 0x00;
    }

    if (scancode < 128u && ps2_scancode_table[scancode] != 0x00)
    {
        ps2_escape_sequence = 0u;

        const char *table = (ps2_shift_state || ps2_caps_lock_state) ?
                            ps2_scancode_table_shift : ps2_scancode_table;
        return table[scancode];
    }

    ps2_escape_sequence = 0u;
    return 0x00;
}

uint8_t ps2_keyboard_get_shift_state(void)
{
    return ps2_shift_state;
}

uint8_t ps2_keyboard_get_ctrl_state(void)
{
    return ps2_ctrl_state;
}

uint8_t ps2_keyboard_get_alt_state(void)
{
    return ps2_alt_state;
}

uint8_t ps2_keyboard_get_caps_lock_state(void)
{
    return ps2_caps_lock_state;
}

void ps2_keyboard_reset_modifiers(void)
{
    ps2_shift_state = 0u;
    ps2_ctrl_state = 0u;
    ps2_alt_state = 0u;
    ps2_caps_lock_state = 0u;
    ps2_escape_sequence = 0u;
}
