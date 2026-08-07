#include <stddef.h>

#include <kernel/diag/telemetry.h>

/* Longest key the duplicate check stores. A key longer than this is compared on
   its first characters only, which can only ever report a duplicate that is not
   one — the safe direction for a counter that is expected to read zero. */
#define TELEMETRY_MAX_KEY_LENGTH 31u

static Serial_t *telemetry_serial = NULL;
static bool telemetry_record_is_open = false;
static uint32_t telemetry_field_count = 0u;

static char telemetry_seen_keys[KERNEL_TELEMETRY_MAX_FIELDS_PER_RECORD][TELEMETRY_MAX_KEY_LENGTH + 1u];

static uint32_t telemetry_record_count = 0u;
static uint32_t telemetry_duplicate_key_count = 0u;
static uint32_t telemetry_sanitised_character_count = 0u;
static uint32_t telemetry_dropped_field_count = 0u;

static bool telemetry_keys_are_equal(const char *lhs, const char *rhs)
{
    while (*lhs && *rhs)
    {
        if (*lhs != *rhs)
            return false;
        ++lhs;
        ++rhs;
    }

    return *lhs == '\0' && *rhs == '\0';
}

static void telemetry_remember_key(const char *key)
{
    if (telemetry_field_count >= KERNEL_TELEMETRY_MAX_FIELDS_PER_RECORD)
        return;

    char *slot = telemetry_seen_keys[telemetry_field_count];
    uint32_t length = 0u;

    while (key[length] != '\0' && length < TELEMETRY_MAX_KEY_LENGTH)
    {
        slot[length] = key[length];
        ++length;
    }

    slot[length] = '\0';
}

static bool telemetry_key_was_already_written(const char *key)
{
    for (uint32_t index = 0u; index < telemetry_field_count; ++index)
    {
        if (telemetry_keys_are_equal(telemetry_seen_keys[index], key))
            return true;
    }

    return false;
}

/* Opens a field and reports whether the caller may write its value. Everything a
   field needs before its value — the guards, the duplicate check, the separator
   and the "key=" — happens here, so the four write_* entry points differ only in
   how they render the value. */
static bool telemetry_open_field(const char *key)
{
    if (!telemetry_record_is_open || !telemetry_serial || !key)
        return false;

    if (telemetry_field_count >= KERNEL_TELEMETRY_MAX_FIELDS_PER_RECORD)
    {
        ++telemetry_dropped_field_count;
        return false;
    }

    if (telemetry_key_was_already_written(key))
        ++telemetry_duplicate_key_count;

    telemetry_remember_key(key);
    ++telemetry_field_count;

    serial_write_char(telemetry_serial, ' ');
    serial_write_string(telemetry_serial, key);
    serial_write_char(telemetry_serial, '=');
    return true;
}

/* A space or an '=' inside a value splits the field in two for any reader, so it
   is replaced rather than emitted, and counted so a check can notice. */
static void telemetry_write_sanitised_text(const char *text)
{
    for (const char *cursor = text; *cursor != '\0'; ++cursor)
    {
        if (*cursor == ' ' || *cursor == '=')
        {
            ++telemetry_sanitised_character_count;
            serial_write_char(telemetry_serial, '_');
            continue;
        }

        serial_write_char(telemetry_serial, *cursor);
    }
}

void kernel_telemetry_begin_record(Serial_t *serial, const char *domain)
{
    if (!serial || !domain)
        return;

    telemetry_serial = serial;
    telemetry_record_is_open = true;
    telemetry_field_count = 0u;

    serial_write_string(serial, KERNEL_TELEMETRY_RECORD_PREFIX " ");
    telemetry_write_sanitised_text(domain);
}

void kernel_telemetry_write_unsigned(const char *key, uint32_t value)
{
    if (!telemetry_open_field(key))
        return;

    serial_write_int(telemetry_serial, (int32_t) value);
}

void kernel_telemetry_write_hexadecimal(const char *key, uint32_t value)
{
    if (!telemetry_open_field(key))
        return;

    serial_write_hex32(telemetry_serial, value);
}

void kernel_telemetry_write_boolean(const char *key, bool value)
{
    if (!telemetry_open_field(key))
        return;

    serial_write_int(telemetry_serial, value ? 1 : 0);
}

void kernel_telemetry_write_text(const char *key, const char *value)
{
    if (!telemetry_open_field(key))
        return;

    telemetry_write_sanitised_text(value ? value : "none");
}

void kernel_telemetry_end_record(void)
{
    if (!telemetry_record_is_open || !telemetry_serial)
        return;

    serial_write_char(telemetry_serial, '\n');
    telemetry_record_is_open = false;
    telemetry_field_count = 0u;
    ++telemetry_record_count;
}

uint32_t kernel_telemetry_get_record_count(void) { return telemetry_record_count; }

uint32_t kernel_telemetry_get_duplicate_key_count(void) { return telemetry_duplicate_key_count; }

uint32_t kernel_telemetry_get_sanitised_character_count(void) { return telemetry_sanitised_character_count; }

uint32_t kernel_telemetry_get_dropped_field_count(void) { return telemetry_dropped_field_count; }

void kernel_telemetry_report(Serial_t *serial)
{
    if (!serial)
        return;

    const uint32_t records_before = telemetry_record_count;

    kernel_telemetry_begin_record(serial, "telemetry");
    kernel_telemetry_write_unsigned("records", records_before);
    kernel_telemetry_write_unsigned("duplicate_keys", telemetry_duplicate_key_count);
    kernel_telemetry_write_unsigned("sanitised_characters", telemetry_sanitised_character_count);
    kernel_telemetry_write_unsigned("dropped_fields", telemetry_dropped_field_count);
    kernel_telemetry_end_record();
}
