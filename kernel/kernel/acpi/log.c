#include <kernel/acpi/log.h>

#include <string.h>

#define LOG_CUT_MARK        "..." /**< What ends a line a field did not fit in. */
#define LOG_CUT_MARK_LENGTH 3u    /**< Its characters. */

typedef AdvancedConfigurationAndPowerInterfaceLogLine_t LogLine_t;

static const char *const log_level_names[] = {"unknown", "error", "warning", "debug"};

/**
 * @brief What a character of a value is written as: itself when it is printable and neither a space nor an
 *        `=`, which would split the field for its reader, `_` otherwise.
 */
static char log_safe_character(char character)
{
    return (character > ' ' && character <= '~' && character != '=') ? character : '_';
}

/**
 * @brief Ends a line that a character did not fit in with LOG_CUT_MARK, which it then keeps.
 */
static void log_cut(LogLine_t *line)
{
    line->length = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_LINE_BYTES - 1u;
    memcpy(line->text + line->length - LOG_CUT_MARK_LENGTH, LOG_CUT_MARK, LOG_CUT_MARK_LENGTH);
    line->text[line->length] = '\0';
    line->cut = true;
}

/**
 * @brief Appends one character to a line, unless it was cut.
 */
static void log_put(LogLine_t *line, char character)
{
    if (line->cut)
        return;
    if (line->length == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_LINE_BYTES - 1u)
    {
        log_cut(line);
        return;
    }
    line->text[line->length++] = character;
    line->text[line->length] = '\0';
}

/**
 * @brief Appends the characters of @p text, as they are.
 */
static void log_put_text(LogLine_t *line, const char *text)
{
    while (*text != '\0')
        log_put(line, *text++);
}

/**
 * @brief Appends a space, then `key=`.
 */
static void log_put_key(LogLine_t *line, const char *key)
{
    log_put(line, ' ');
    log_put_text(line, key);
    log_put(line, '=');
}

/**
 * @brief Writes a line to the host's log at @p level, when the host keeps one.
 */
static void log_write(const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint8_t level, const LogLine_t *line)
{
    if (host != NULL && host->log != NULL)
        host->log(host->context, level, line->text);
}

void advanced_configuration_and_power_interface_log_open(LogLine_t *line, const char *event)
{
    line->length = 0u;
    line->text[0] = '\0';
    line->cut = false;
    log_put_text(line, event);
}

void advanced_configuration_and_power_interface_log_text(LogLine_t *line, const char *key, const char *value)
{
    log_put_key(line, key);
    while (*value != '\0')
        log_put(line, log_safe_character(*value++));
}

void advanced_configuration_and_power_interface_log_characters(LogLine_t *line, const char *key, const char *characters,
                                                               uint32_t count)
{
    log_put_key(line, key);
    for (uint32_t index = 0u; index < count; ++index)
        log_put(line, log_safe_character(characters[index]));
}

void advanced_configuration_and_power_interface_log_hexadecimal(LogLine_t *line, const char *key, uint64_t value,
                                                                uint32_t digits)
{
    static const char alphabet[] = "0123456789ABCDEF";

    log_put_key(line, key);
    log_put_text(line, "0x");
    for (uint32_t digit = digits; digit != 0u; --digit)
        log_put(line, alphabet[(value >> (4u * (digit - 1u))) & 0xFu]);
}

void advanced_configuration_and_power_interface_log_unsigned(LogLine_t *line, const char *key, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;

    log_put_key(line, key);
    do
    {
        digits[count++] = (char) ('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u)
        log_put(line, digits[--count]);
}

void advanced_configuration_and_power_interface_log_table(
    LogLine_t *line, const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header)
{
    uint32_t identifier = (uint32_t) sizeof(header->oem_table_id);

    while (identifier != 0u &&
           (header->oem_table_id[identifier - 1u] == ' ' || header->oem_table_id[identifier - 1u] == '\0'))
        --identifier;
    advanced_configuration_and_power_interface_log_characters(line, "table", header->signature,
                                                              (uint32_t) sizeof(header->signature));
    log_put(line, '/');
    for (uint32_t index = 0u; index < identifier; ++index)
        log_put(line, log_safe_character(header->oem_table_id[index]));
}

void advanced_configuration_and_power_interface_log_error(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                                          const LogLine_t *line)
{
    log_write(host, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_ERROR, line);
}

void advanced_configuration_and_power_interface_log_warning(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                                            const LogLine_t *line)
{
    log_write(host, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_WARNING, line);
}

#if ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_BUILD_LEVEL >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_DEBUG
void advanced_configuration_and_power_interface_log_debug(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                                          const LogLine_t *line)
{
    log_write(host, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_LOG_DEBUG, line);
}
#endif

const char *advanced_configuration_and_power_interface_log_level_name(uint8_t level)
{
    return (level < sizeof(log_level_names) / sizeof(log_level_names[0])) ? log_level_names[level] : log_level_names[0];
}
