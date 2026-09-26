#include <kernel/drivers/ps2_keyboard.h>
#include <stdint.h>

/**
 * @name United States QWERTY
 *
 * Scan code Set 1 layout tables, indexed by the 7-bit make code (scancode & 0x7F). An
 * entry of 0x00 means the key produces nothing in that modifier level. Three levels are
 * provided per layout: base (no modifier), shift, and AltGr (right alt).
 * @{
 */
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

/** Standard US QWERTY has no AltGr level. */
static const char personal_system_2_layout_us_altgr[128] = {0};
/** @} */

/**
 * @name French AZERTY
 *
 * Letters follow the AZERTY positions, the number row produces its symbols at the base
 * level and digits under shift, and AltGr exposes the programmer symbols
 * (@ # { [ ] } | \ ` ^ ~). Dead keys (circumflex/diaeresis) are simplified: the
 * circumflex key yields a literal '^' and the diaeresis level is left unmapped. Validate
 * against `qemu -k fr` before relying on it.
 *
 * Non-ASCII characters are encoded as CP437 bytes, which is the code page rendered by
 * the VGA text-mode font used by this kernel.
 * @{
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

/** @} */

/**
 * @brief Modifier and layout state.
 * @details
 * These are only mutated from personal_system_2_keyboard_decode_scancode, which the driver
 * drives exclusively from the bottom-half drain (never from the IRQ handler),
 * so no synchronisation is required.
 */
static uint8_t personal_system_2_shift_left_state = 0u;
static uint8_t personal_system_2_shift_right_state = 0u;
static uint8_t personal_system_2_ctrl_left_state = 0u;
static uint8_t personal_system_2_ctrl_right_state = 0u;
static uint8_t personal_system_2_alt_left_state = 0u;
static uint8_t personal_system_2_alt_right_state = 0u; /**< AltGr */
static uint8_t personal_system_2_caps_lock_state = 0u;
static uint8_t personal_system_2_extended_pending = 0u; /**< 0xE0 seen, applies to next byte */

/**
 * @brief Which keys are DOWN right now, one bit per Set-1 make code.
 * @details
 * The driver already saw every release: bit 7 of a scancode is the break flag, and
 * decode_scancode read it, used it for the modifiers, and threw the rest away —
 * which is why the engine could be told "the walker typed W" and never "the walker
* is HOLDING W". A character stream is the right shape for a console and the wrong
 * shape for a body that walks: holding a direction is a state, not an event, and
 * rebuilding it from key repeat gives the stutter the repeat delay is made of.
 *
 * Updated on the consumer side, in decode_scancode, like the modifier state and for
 * the same reason: assembling state in interrupt context races with whoever reads
 * it. It is therefore only as fresh as the last drain of the ring — a caller that
 * stops draining sees keys stay down, which is correct, because it also stopped
 * seeing them come up.
 */
static uint8_t personal_system_2_key_down_bitmap[16] = {0};

static PersonalSystem2KeyboardLayout_t personal_system_2_active_layout =
    PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_UNITED_STATES_QWERTY;

/** Set-1 prefix byte announcing that the next scancode is an extended key. */
#define PERSONAL_SYSTEM_2_EXTENDED_PREFIX 0xE0u

/** Set-1 make codes of the modifier keys. Ctrl and Alt are the right-hand keys when extended. */
#define PERSONAL_SYSTEM_2_MAKE_CODE_LEFT_SHIFT  0x2Au
#define PERSONAL_SYSTEM_2_MAKE_CODE_RIGHT_SHIFT 0x36u
#define PERSONAL_SYSTEM_2_MAKE_CODE_CTRL        0x1Du
#define PERSONAL_SYSTEM_2_MAKE_CODE_ALT         0x38u
#define PERSONAL_SYSTEM_2_MAKE_CODE_CAPS_LOCK   0x3Au

/**
 * @brief Records whether an unextended key is down, in the key-down bitmap.
 *
 * @details Called before any modifier or mapping decision, so modifiers, dead keys and
 *          unmapped codes are recorded too: they are all keys someone can hold.
 *
 * @note Extended codes are deliberately NOT recorded: they share the low 7 bits with
 *       unextended ones, so right ctrl would otherwise clear left ctrl's bit.
 *
 * @param code    Set-1 make code, 0..127.
 * @param pressed 1 on make, 0 on break.
 */
static void personal_system_2_keyboard_record_key_state(uint8_t code, uint8_t pressed)
{
    const uint8_t index = (uint8_t) (code >> 3);
    const uint8_t mask = (uint8_t) (1u << (code & 7u));
    if (pressed)
        personal_system_2_key_down_bitmap[index] |= mask;
    else
        personal_system_2_key_down_bitmap[index] &= (uint8_t) ~mask;
}

/**
 * @brief Tracks the right-hand ctrl and alt (AltGr), the only extended keys that matter here.
 *
 * @note Every other extended key — arrows, keypad enter and the like — produces nothing.
 *
 * @param code    Set-1 make code, without its extended prefix.
 * @param pressed 1 on make, 0 on break.
 */
static void personal_system_2_keyboard_track_extended_modifier(uint8_t code, uint8_t pressed)
{
    if (code == PERSONAL_SYSTEM_2_MAKE_CODE_CTRL)
        personal_system_2_ctrl_right_state = pressed;
    else if (code == PERSONAL_SYSTEM_2_MAKE_CODE_ALT)
        personal_system_2_alt_right_state = pressed;
}

/**
 * @brief Tracks the unextended modifiers: both shifts, left ctrl, left alt and caps lock.
 *
 * @note Caps lock toggles on press only.
 *
 * @param code    Set-1 make code.
 * @param pressed 1 on make, 0 on break.
 * @return 1 when @p code was a modifier, which produces no character.
 */
static uint8_t personal_system_2_keyboard_track_modifier(uint8_t code, uint8_t pressed)
{
    switch (code)
    {
    case PERSONAL_SYSTEM_2_MAKE_CODE_LEFT_SHIFT: personal_system_2_shift_left_state = pressed; return 1u;
    case PERSONAL_SYSTEM_2_MAKE_CODE_RIGHT_SHIFT: personal_system_2_shift_right_state = pressed; return 1u;
    case PERSONAL_SYSTEM_2_MAKE_CODE_CTRL: personal_system_2_ctrl_left_state = pressed; return 1u;
    case PERSONAL_SYSTEM_2_MAKE_CODE_ALT: personal_system_2_alt_left_state = pressed; return 1u;
    case PERSONAL_SYSTEM_2_MAKE_CODE_CAPS_LOCK:
        if (pressed)
            personal_system_2_caps_lock_state ^= 1u;
        return 1u;
    default: return 0u;
    }
}

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
    if (scancode == PERSONAL_SYSTEM_2_EXTENDED_PREFIX)
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
        personal_system_2_keyboard_track_extended_modifier(code, pressed);
        return 0x00;
    }

    personal_system_2_keyboard_record_key_state(code, pressed);
    if (personal_system_2_keyboard_track_modifier(code, pressed))
        return 0x00;

    if (is_break)
        return 0x00;

    return personal_system_2_keyboard_translate_make_code(code);
}

uint8_t personal_system_2_keyboard_is_code_held(uint8_t code)
{
    if (code >= 128u)
        return 0u;
    return (uint8_t) ((personal_system_2_key_down_bitmap[code >> 3] & (1u << (code & 7u))) ? 1u : 0u);
}

uint8_t personal_system_2_keyboard_is_character_held(char character)
{
    const char *base = personal_system_2_layout_us_base;

    if (personal_system_2_active_layout == PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_FRENCH_AZERTY)
        base = personal_system_2_layout_fr_base;

    for (uint16_t code = 0u; code < 128u; ++code)
    {
        if (base[code] != character)
            continue;
        if (personal_system_2_keyboard_is_code_held((uint8_t) code))
            return 1u;
    }
    return 0u;
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
    for (uint8_t i = 0u; i < 16u; ++i)
        personal_system_2_key_down_bitmap[i] = 0u;
}
