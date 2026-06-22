/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** PS/2 keyboard scan code decoder (Set 1)
*/

#include <kernel/drivers/ps2_keyboard.h>
#include <stdint.h>

/*
** Scan code Set 1 layout tables.
**
** Indexed by the 7-bit make code (scancode & 0x7F). An entry of 0x00 means the
** key produces nothing in that modifier level. Three levels are provided per
** layout: base (no modifier), shift, and AltGr (right alt).
**
** Non-ASCII French characters are encoded as CP437 bytes, which is the code
** page rendered by the VGA text-mode font used by this kernel.
*/

/* ---- United States QWERTY -------------------------------------------- */

static const char personal_system_2_layout_us_base[128] = {
    0x00, '\033', '1',  '2',  '3',  '4',  '5',  '6', '7',  '8',  /* 0x00-0x09 */
    '9',  '0',    '-',  '=',  '\b', '\t', 'q',  'w', 'e',  'r',  /* 0x0A-0x13 */
    't',  'y',    'u',  'i',  'o',  'p',  '[',  ']', '\n', 0x00, /* 0x14-0x1D */
    'a',  's',    'd',  'f',  'g',  'h',  'j',  'k', 'l',  ';',  /* 0x1E-0x27 */
    '\'', '`',    0x00, '\\', 'z',  'x',  'c',  'v', 'b',  'n',  /* 0x28-0x31 */
    'm',  ',',    '.',  '/',  0x00, '*',  0x00, ' ', 0x00, 0x00, /* 0x32-0x3B */
};

static const char personal_system_2_layout_us_shift[128] = {
    0x00, '\033', '!',  '@', '#',  '$',  '%',  '^', '&',  '*',  /* 0x00-0x09 */
    '(',  ')',    '_',  '+', '\b', '\t', 'Q',  'W', 'E',  'R',  /* 0x0A-0x13 */
    'T',  'Y',    'U',  'I', 'O',  'P',  '{',  '}', '\n', 0x00, /* 0x14-0x1D */
    'A',  'S',    'D',  'F', 'G',  'H',  'J',  'K', 'L',  ':',  /* 0x1E-0x27 */
    '\"', '~',    0x00, '|', 'Z',  'X',  'C',  'V', 'B',  'N',  /* 0x28-0x31 */
    'M',  '<',    '>',  '?', 0x00, '*',  0x00, ' ', 0x00, 0x00, /* 0x32-0x3B */
};

/* Standard US QWERTY has no AltGr level. */
static const char personal_system_2_layout_us_altgr[128] = {0};

/* ---- French AZERTY --------------------------------------------------- */
/*
** Letters follow the AZERTY positions, the number row produces its symbols at
** the base level and digits under shift, and AltGr exposes the programmer
** symbols (@ # { [ ] } | \ ` ^ ~). Dead keys (circumflex/diaeresis) are
** simplified: the circumflex key yields a literal '^' and the diaeresis level
** is left unmapped. Validate against `qemu -k fr` before relying on it.
*/

static const char personal_system_2_layout_fr_base[128] = {
    [0x01] = '\033',
    [0x02] = '&',
    [0x03] = (char) 0x82 /* é */,
    [0x04] = '"',
    [0x05] = '\'',
    [0x06] = '(',
    [0x07] = '-',
    [0x08] = (char) 0x8A /* è */,
    [0x09] = '_',
    [0x0A] = (char) 0x87 /* ç */,
    [0x0B] = (char) 0x85 /* à */,
    [0x0C] = ')',
    [0x0D] = '=',
    [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'a',
    [0x11] = 'z',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '^',
    [0x1B] = '$',
    [0x1C] = '\n',
    [0x1E] = 'q',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = 'm',
    [0x28] = (char) 0x97 /* ù */,
    [0x29] = (char) 0xFD /* ² */,
    [0x2B] = '*',
    [0x2C] = 'w',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = ',',
    [0x33] = ';',
    [0x34] = ':',
    [0x35] = '!',
    [0x37] = '*',
    [0x39] = ' ',
};

static const char personal_system_2_layout_fr_shift[128] = {
    [0x01] = '\033',
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = (char) 0xF8 /* ° */,
    [0x0D] = '+',
    [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'A',
    [0x11] = 'Z',
    [0x12] = 'E',
    [0x13] = 'R',
    [0x14] = 'T',
    [0x15] = 'Y',
    [0x16] = 'U',
    [0x17] = 'I',
    [0x18] = 'O',
    [0x19] = 'P',
    [0x1B] = (char) 0x9C /* £ */,
    [0x1C] = '\n',
    [0x1E] = 'Q',
    [0x1F] = 'S',
    [0x20] = 'D',
    [0x21] = 'F',
    [0x22] = 'G',
    [0x23] = 'H',
    [0x24] = 'J',
    [0x25] = 'K',
    [0x26] = 'L',
    [0x27] = 'M',
    [0x28] = '%',
    [0x29] = (char) 0xFC /* ³ */,
    [0x2B] = (char) 0xE6 /* µ */,
    [0x2C] = 'W',
    [0x2D] = 'X',
    [0x2E] = 'C',
    [0x2F] = 'V',
    [0x30] = 'B',
    [0x31] = 'N',
    [0x32] = '?',
    [0x33] = '.',
    [0x34] = '/',
    [0x35] = (char) 0x15 /* § */,
    [0x37] = '*',
    [0x39] = ' ',
};

static const char personal_system_2_layout_fr_altgr[128] = {
    [0x03] = '~',  [0x04] = '#', [0x05] = '{', [0x06] = '[', [0x07] = '|', [0x08] = '`',
    [0x09] = '\\', [0x0A] = '^', [0x0B] = '@', [0x0C] = ']', [0x0D] = '}',
};

/* ---- Modifier and layout state --------------------------------------- */
/*
** These are only mutated from personal_system_2_keyboard_decode_scancode, which the driver
** drives exclusively from the bottom-half drain (never from the IRQ handler),
** so no synchronisation is required.
*/
static uint8_t personal_system_2_shift_left_state = 0u;
static uint8_t personal_system_2_shift_right_state = 0u;
static uint8_t personal_system_2_ctrl_left_state = 0u;
static uint8_t personal_system_2_ctrl_right_state = 0u;
static uint8_t personal_system_2_alt_left_state = 0u;
static uint8_t personal_system_2_alt_right_state = 0u; /* AltGr */
static uint8_t personal_system_2_caps_lock_state = 0u;
static uint8_t personal_system_2_extended_pending = 0u; /* 0xE0 seen, applies to next byte */

static PersonalSystem2KeyboardLayout_t personal_system_2_active_layout =
    PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_UNITED_STATES_QWERTY;

static void personal_system_2_keyboard_select_tables(const char **out_base, const char **out_shift,
                                                     const char **out_altgr)
{
    switch (personal_system_2_active_layout)
    {
    case PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_FRENCH_AZERTY:
        *out_base = personal_system_2_layout_fr_base;
        *out_shift = personal_system_2_layout_fr_shift;
        *out_altgr = personal_system_2_layout_fr_altgr;
        return;
    case PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_UNITED_STATES_QWERTY:
    default:
        *out_base = personal_system_2_layout_us_base;
        *out_shift = personal_system_2_layout_us_shift;
        *out_altgr = personal_system_2_layout_us_altgr;
        return;
    }
}

static char personal_system_2_keyboard_translate_make_code(uint8_t code)
{
    const char *base = (const char *) 0;
    const char *shift = (const char *) 0;
    const char *altgr = (const char *) 0;

    if (code >= 128u)
        return 0x00;

    personal_system_2_keyboard_select_tables(&base, &shift, &altgr);

    char decoded;
    if (personal_system_2_alt_right_state)
        decoded = altgr[code];
    else if (personal_system_2_shift_left_state || personal_system_2_shift_right_state)
        decoded = shift[code];
    else
        decoded = base[code];

    if (personal_system_2_caps_lock_state)
    {
        if (decoded >= 'a' && decoded <= 'z')
            decoded = (char) (decoded - 'a' + 'A');
        else if (decoded >= 'A' && decoded <= 'Z')
            decoded = (char) (decoded - 'A' + 'a');
    }

    return decoded;
}

char personal_system_2_keyboard_decode_scancode(uint8_t scancode)
{
    if (scancode == 0xE0u)
    {
        personal_system_2_extended_pending = 1u;
        return 0x00;
    }

    const uint8_t is_break = (uint8_t) ((scancode & 0x80u) ? 1u : 0u);
    const uint8_t pressed = (uint8_t) (!is_break);
    const uint8_t code = (uint8_t) (scancode & 0x7Fu);
    const uint8_t extended = personal_system_2_extended_pending;

    personal_system_2_extended_pending = 0u;

    if (extended)
    {
        switch (code)
        {
        case 0x1Du: /* right ctrl */ personal_system_2_ctrl_right_state = pressed; return 0x00;
        case 0x38u: /* right alt (AltGr) */ personal_system_2_alt_right_state = pressed; return 0x00;
        default: /* arrows, keypad enter, etc.: not produced here */ return 0x00;
        }
    }

    switch (code)
    {
    case 0x2Au: /* left shift */ personal_system_2_shift_left_state = pressed; return 0x00;
    case 0x36u: /* right shift */ personal_system_2_shift_right_state = pressed; return 0x00;
    case 0x1Du: /* left ctrl */ personal_system_2_ctrl_left_state = pressed; return 0x00;
    case 0x38u: /* left alt */ personal_system_2_alt_left_state = pressed; return 0x00;
    case 0x3Au: /* caps lock toggles on press only */
        if (pressed)
            personal_system_2_caps_lock_state ^= 1u;
        return 0x00;
    default: break;
    }

    if (is_break)
        return 0x00;

    return personal_system_2_keyboard_translate_make_code(code);
}

void personal_system_2_keyboard_set_layout(PersonalSystem2KeyboardLayout_t layout)
{
    personal_system_2_active_layout = layout;
}

PersonalSystem2KeyboardLayout_t personal_system_2_keyboard_get_layout(void) { return personal_system_2_active_layout; }

uint8_t personal_system_2_keyboard_get_shift_state(void)
{
    return (uint8_t) ((personal_system_2_shift_left_state || personal_system_2_shift_right_state) ? 1u : 0u);
}

uint8_t personal_system_2_keyboard_get_ctrl_state(void)
{
    return (uint8_t) ((personal_system_2_ctrl_left_state || personal_system_2_ctrl_right_state) ? 1u : 0u);
}

uint8_t personal_system_2_keyboard_get_alt_state(void)
{
    return (uint8_t) ((personal_system_2_alt_left_state || personal_system_2_alt_right_state) ? 1u : 0u);
}

uint8_t personal_system_2_keyboard_get_caps_lock_state(void) { return personal_system_2_caps_lock_state; }

void personal_system_2_keyboard_reset_modifiers(void)
{
    personal_system_2_shift_left_state = 0u;
    personal_system_2_shift_right_state = 0u;
    personal_system_2_ctrl_left_state = 0u;
    personal_system_2_ctrl_right_state = 0u;
    personal_system_2_alt_left_state = 0u;
    personal_system_2_alt_right_state = 0u;
    personal_system_2_caps_lock_state = 0u;
    personal_system_2_extended_pending = 0u;
}
