#include "recorded_machine.h"

#include <kernel/acpi/tables.h>
#include <kernel/cpu/helpers/acpi_helper.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORDED_MACHINE_FIRST_ADDRESS      0x00100000u /**< Where the tables the FADT does not name are served from. */
#define RECORDED_MACHINE_ALIGNMENT          16u         /**< Alignment of each table served there. */
#define RECORDED_MACHINE_MAXIMUM_LENGTH     0x01000000u /**< A longer header length is taken for a corrupt one. */
#define RECORDED_MACHINE_ROOT_POINTER_BYTES 48u         /**< Bytes kept for the root pointer, before the XSDT. */
#define RECORDED_MACHINE_FADT_DSDT          40u         /**< Offset of a FADT's 32-bit DSDT field. */
#define RECORDED_MACHINE_FADT_EXTENDED_DSDT 140u        /**< Offset of a FADT's X_DSDT field. */
#define RECORDED_MACHINE_FADT_FACS          36u         /**< Offset of a FADT's FIRMWARE_CTRL field. */
#define RECORDED_MACHINE_FADT_EXTENDED_FACS 132u        /**< Offset of a FADT's X_FIRMWARE_CTRL field. */
#define RECORDED_MACHINE_HEADER_BYTES       36u         /**< Bytes of a table header. */
#define RECORDED_MACHINE_TABLE_IDENTIFIER   16u         /**< Offset of a header's OEM table ID, 8 bytes. */
#define RECORDED_MACHINE_STRING_PREFIX      0x0Du       /**< AML's StringPrefix. */
#define RECORDED_MACHINE_MAX_PLACE                                                                                     \
    0x100000000ull /**< The first address a table placed for Load may not                                              \
                        reach: i686 reads none past 4 GiB. */

/**
 * @brief The value of the hexadecimal digit @p c, or -1.
 */
static int recorded_machine_hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/**
 * @brief Whether @p line opens a table, as in `DSDT @ 0x0000000000000000`.
 */
static bool recorded_machine_is_header(const char *line)
{
    const char *at = strstr(line, " @ 0x");

    if (at == NULL || at == line || at - line > 8 || isspace((unsigned char) line[0]))
        return false;
    for (const char *c = at + 5; *c != '\0' && *c != '\n'; ++c)
    {
        if (recorded_machine_hex_digit(*c) < 0 && !isspace((unsigned char) *c))
            return false;
    }
    return true;
}

/**
 * @brief Grows @p table to at least @p length bytes, the new ones zero, doubling its capacity when
 *        it runs out so that a dump is read in linear time.
 */
static bool recorded_machine_grow(RecordedTable_t *table, uint32_t length)
{
    if (length <= table->length)
        return true;
    if (length > table->capacity)
    {
        const uint32_t capacity = (length > 2u * table->capacity) ? length : 2u * table->capacity;
        uint8_t *bytes = realloc(table->bytes, capacity);

        if (bytes == NULL)
            return false;
        table->bytes = bytes;
        table->capacity = capacity;
    }
    memset(table->bytes + table->length, 0, length - table->length);
    table->length = length;
    return true;
}

/**
 * @brief Adds the bytes of a line such as `    0010: 01 02 ... 0F  ................` to @p table,
 *        at the offset the line gives.
 *
 * @return false when the line holds no bytes, or they cannot be stored.
 */
static bool recorded_machine_read_row(RecordedTable_t *table, const char *line)
{
    const char *c = line;
    uint32_t offset = 0u;
    uint32_t digits = 0u;

    if (!isspace((unsigned char) *c))
        return false;
    while (isspace((unsigned char) *c))
        ++c;
    for (; recorded_machine_hex_digit(*c) >= 0 && digits < 8u; ++c, ++digits)
        offset = offset * 16u + (uint32_t) recorded_machine_hex_digit(*c);
    if (digits < 4u || *c != ':')
        return false;
    ++c;

    uint8_t row[16];
    uint32_t count = 0u;

    while (count < 16u && c[0] == ' ' && recorded_machine_hex_digit(c[1]) >= 0 && recorded_machine_hex_digit(c[2]) >= 0)
    {
        row[count++] = (uint8_t) (recorded_machine_hex_digit(c[1]) * 16 + recorded_machine_hex_digit(c[2]));
        c += 3;
    }
    if (count == 0u || offset > RECORDED_MACHINE_MAXIMUM_LENGTH || !recorded_machine_grow(table, offset + count))
        return false;
    memcpy(table->bytes + offset, row, count);
    return true;
}

/**
 * @brief Whether @p line, its line ending apart, is @p marker.
 */
static bool recorded_machine_is_marker(const char *line, const char *marker)
{
    const size_t length = strlen(marker);

    return strncmp(line, marker, length) == 0 && (line[length] == '\0' || line[length] == '\n' || line[length] == '\r');
}

/**
 * @brief Reads one line of a dump: a header opens a table, a row adds its bytes to the table open.
 *
 * @return false when a header opens one table more than a recorded machine holds.
 */
static bool recorded_machine_read_line(RecordedMachine_t *machine, const char *line, RecordedTable_t **table)
{
    if (recorded_machine_is_header(line))
    {
        if (machine->table_count == RECORDED_MACHINE_MAX_TABLES)
            return false;
        *table = &machine->tables[machine->table_count++];
    }
    else if (*table != NULL)
    {
        (void) recorded_machine_read_row(*table, line);
    }
    return true;
}

/**
 * @brief Reads the tables of the dump at @p path; when @p printed, only those a kernel printed
 *        between ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_BEGIN and
 *        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_END, the first time it did.
 */
static bool recorded_machine_read(RecordedMachine_t *machine, const char *path, bool printed, const char **error)
{
    FILE *file = fopen(path, "r");

    if (file == NULL)
    {
        *error = printed ? "the log cannot be opened" : "the dump cannot be opened";
        return false;
    }

    char line[512];
    RecordedTable_t *table = NULL;
    bool fits = true;
    bool begun = !printed;
    bool ended = false;

    while (fits && !ended && fgets(line, sizeof(line), file) != NULL)
    {
        if (!begun)
            begun = recorded_machine_is_marker(line, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_BEGIN);
        else if (printed &&
                 recorded_machine_is_marker(line, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_END))
            ended = true;
        else
            fits = recorded_machine_read_line(machine, line, &table);
    }
    fclose(file);

    const char *wrong = NULL;

    if (!fits)
        wrong = printed ? "the log holds more tables than a recorded machine" :
                          "the dump holds more tables than a recorded machine";
    else if (!begun)
        wrong = "the log holds no tables a kernel printed";
    else if (printed && !ended)
        wrong = "the tables the kernel printed are cut short: the log ends before their end";
    else if (machine->table_count == 0u)
        wrong = printed ? "the kernel printed no table" : "the dump holds no table";
    if (wrong != NULL)
        *error = wrong;
    return wrong == NULL;
}

/**
 * @brief The little-endian value of the @p width bytes at @p offset of @p table, or 0 when there is
 *        no table or it ends before them.
 */
static uint64_t recorded_machine_read_field(const RecordedTable_t *table, uint32_t offset, uint32_t width)
{
    uint64_t value = 0u;

    if (table == NULL || table->length < offset + width)
        return 0u;
    memcpy(&value, table->bytes + offset, width);
    return value;
}

/**
 * @brief The first table of @p machine whose signature is @p signature, or NULL.
 */
static RecordedTable_t *recorded_machine_find(RecordedMachine_t *machine, const char *signature)
{
    for (uint32_t index = 0u; index < machine->table_count; ++index)
    {
        if (memcmp(machine->tables[index].signature, signature, 4u) == 0)
            return &machine->tables[index];
    }
    return NULL;
}

/**
 * @brief Whether the harness writes @p table's place itself: the root pointer and the root tables of
 *        the dump, and the FACS, which no root table lists.
 */
static bool recorded_machine_is_left_out(const RecordedTable_t *table)
{
    return table->length < 4u || memcmp(table->bytes, "RSD ", 4u) == 0 || memcmp(table->signature, "XSDT", 4u) == 0 ||
           memcmp(table->signature, "RSDT", 4u) == 0 || memcmp(table->signature, "FACS", 4u) == 0;
}

/**
 * @brief The first address from @p cursor, aligned, where @p length bytes overlap none of the
 *        @p reserved_count ranges at @p reserved, each @p reserved_length bytes long, nor a table
 *        @p machine serves for Load.
 */
static uint64_t recorded_machine_free_address(const RecordedMachine_t *machine, uint64_t cursor, uint64_t length,
                                              const uint64_t *reserved, uint32_t reserved_count,
                                              uint64_t reserved_length)
{
    bool moved = true;

    while (moved)
    {
        moved = false;
        cursor = (cursor + RECORDED_MACHINE_ALIGNMENT - 1u) & ~(uint64_t) (RECORDED_MACHINE_ALIGNMENT - 1u);
        for (uint32_t index = 0u; index < reserved_count; ++index)
        {
            if (cursor < reserved[index] + reserved_length && reserved[index] < cursor + length)
            {
                cursor = reserved[index] + reserved_length;
                moved = true;
            }
        }
        for (uint32_t index = 0u; index < machine->table_count; ++index)
        {
            const RecordedTable_t *table = &machine->tables[index];

            if (table->placed != 0u && cursor < table->placed + table->length && table->placed < cursor + length)
            {
                cursor = table->placed + table->length;
                moved = true;
            }
        }
    }
    return cursor;
}

/**
 * @brief Sets the byte at @p checksum_offset so that the @p length bytes at @p bytes sum to zero.
 */
static void recorded_machine_seal(uint8_t *bytes, uint32_t length, uint32_t checksum_offset)
{
    uint8_t sum = 0u;

    bytes[checksum_offset] = 0u;
    for (uint32_t index = 0u; index < length; ++index)
        sum = (uint8_t) (sum + bytes[index]);
    bytes[checksum_offset] = (uint8_t) (0u - sum);
}

/**
 * @brief Writes the root pointer at the start of @p root and, RECORDED_MACHINE_ROOT_POINTER_BYTES
 *        further, an XSDT of the @p entry_count addresses at @p entries.
 */
static void recorded_machine_write_root(uint8_t *root, uint64_t address, const uint64_t *entries, uint32_t entry_count)
{
    AdvancedConfigurationAndPowerInterfaceRsdp_t *pointer = (AdvancedConfigurationAndPowerInterfaceRsdp_t *) root;
    AdvancedConfigurationAndPowerInterfaceSdtHeader_t *xsdt =
        (AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) (root + RECORDED_MACHINE_ROOT_POINTER_BYTES);

    memcpy(pointer->signature, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE,
           sizeof(pointer->signature));
    memcpy(pointer->oem_id, "LPLKRN", sizeof(pointer->oem_id));
    pointer->revision = 2u;
    pointer->length = sizeof(*pointer);
    pointer->xsdt_address = address + RECORDED_MACHINE_ROOT_POINTER_BYTES;
    recorded_machine_seal(root,
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ROOT_SYSTEM_DESCRIPTION_POINTER_VERSION_1_LENGTH,
                          offsetof(AdvancedConfigurationAndPowerInterfaceRsdp_t, checksum));
    recorded_machine_seal(root, sizeof(*pointer),
                          offsetof(AdvancedConfigurationAndPowerInterfaceRsdp_t, extended_checksum));

    memcpy(xsdt->signature, "XSDT", sizeof(xsdt->signature));
    xsdt->length = (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t) +
                   entry_count * (uint32_t) sizeof(uint64_t);
    xsdt->revision = 1u;
    memcpy(xsdt->oem_id, "LPLKRN", sizeof(xsdt->oem_id));
    memcpy(xsdt->oem_table_id, "RECORDED", sizeof(xsdt->oem_table_id));
    memcpy((uint8_t *) xsdt + (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t), entries,
           entry_count * sizeof(uint64_t));
    recorded_machine_seal((uint8_t *) xsdt, xsdt->length,
                          offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, checksum));
}

/**
 * @brief Whether @p address is one of the @p count addresses at @p addresses.
 */
static bool recorded_machine_holds(const uint64_t *addresses, uint32_t count, uint64_t address)
{
    for (uint32_t index = 0u; index < count; ++index)
    {
        if (addresses[index] == address)
            return true;
    }
    return false;
}

/**
 * @brief Serves @p dsdt at every address a FADT of @p machine names, by its 32-bit field or by
 *        X_DSDT, whichever FADT the directory takes, and adds those addresses to @p reserved.
 */
static bool recorded_machine_serve_dsdt(RecordedMachine_t *machine, const RecordedTable_t *dsdt, uint64_t *reserved,
                                        uint32_t *reserved_count, const char **error)
{
    for (uint32_t index = 0u; dsdt != NULL && index < machine->table_count; ++index)
    {
        const RecordedTable_t *fadt = &machine->tables[index];

        if (memcmp(fadt->signature, "FACP", 4u) != 0)
            continue;

        const uint64_t named[] = {
            recorded_machine_read_field(fadt, RECORDED_MACHINE_FADT_DSDT, 4u),
            recorded_machine_read_field(fadt, RECORDED_MACHINE_FADT_EXTENDED_DSDT, 8u),
        };

        for (uint32_t field = 0u; field < 2u; ++field)
        {
            if (named[field] == 0u || recorded_machine_holds(reserved, *reserved_count, named[field]))
                continue;
            if (!advanced_configuration_and_power_interface_simulated_host_place(&machine->simulated, named[field],
                                                                                 dsdt->bytes, dsdt->length))
            {
                *error = "the DSDT cannot be served where a FADT names it";
                return false;
            }
            reserved[(*reserved_count)++] = named[field];
        }
    }
    return true;
}

/**
 * @brief Serves the FACS where the first FADT of @p machine names it, as the directory chooses: the
 *        32-bit field when it is not 0, else X_FIRMWARE_CTRL below 4 GiB; and adds that address to
 *        @p reserved. A dump without a FACS, or a FADT that names none, serves nothing.
 */
static bool recorded_machine_serve_facs(RecordedMachine_t *machine, uint64_t *reserved, uint32_t *reserved_count,
                                        const char **error)
{
    const RecordedTable_t *facs = recorded_machine_find(machine, "FACS");
    const RecordedTable_t *fadt = recorded_machine_find(machine, "FACP");

    if (facs == NULL || fadt == NULL)
        return true;

    const uint64_t named_32 = recorded_machine_read_field(fadt, RECORDED_MACHINE_FADT_FACS, 4u);
    const uint64_t named_64 = recorded_machine_read_field(fadt, RECORDED_MACHINE_FADT_EXTENDED_FACS, 8u);
    const uint64_t address = (named_32 != 0u) ? named_32 : ((named_64 >> 32u) == 0u) ? named_64 : 0u;

    if (address == 0u || recorded_machine_holds(reserved, *reserved_count, address))
        return true;
    if (!advanced_configuration_and_power_interface_simulated_host_place(&machine->simulated, address, facs->bytes,
                                                                         facs->length))
    {
        *error = "the FACS cannot be served where the FADT names it";
        return false;
    }
    reserved[(*reserved_count)++] = address;
    return true;
}

/**
 * @brief Reads the AML integer at @p bytes[@p *offset], below @p end: Zero, One, or a BytePrefix,
 *        WordPrefix, DWordPrefix or QWordPrefix constant, and moves @p *offset past it.
 *
 * @return false for any other term.
 */
static bool recorded_machine_read_integer(const uint8_t *bytes, uint32_t end, uint32_t *offset, uint64_t *out_value)
{
    static const uint8_t widths[] = {[0x0A] = 1u, [0x0B] = 2u, [0x0C] = 4u, [0x0E] = 8u};
    const uint8_t prefix = (*offset < end) ? bytes[*offset] : 0xFFu;
    const uint32_t width = (prefix < sizeof(widths)) ? widths[prefix] : 0u;

    if (prefix == 0x00u || prefix == 0x01u)
    {
        *out_value = prefix;
        ++*offset;
        return true;
    }
    if (width == 0u || end - *offset - 1u < width)
        return false;
    *out_value = 0u;
    for (uint32_t index = 0u; index < width; ++index)
        *out_value |= (uint64_t) bytes[*offset + 1u + index] << (8u * index);
    *offset += 1u + width;
    return true;
}

/**
 * @brief Whether the @p length characters at @p text name the OEM table ID @p identifier: the same
 *        letters, case apart, trailing spaces and NULs apart, as PmRef's labels name the tables.
 */
static bool recorded_machine_names_table(const uint8_t *text, uint32_t length, const uint8_t *identifier)
{
    uint32_t wanted = 8u;

    while (wanted != 0u && (identifier[wanted - 1u] == ' ' || identifier[wanted - 1u] == 0u))
        --wanted;
    while (length != 0u && text[length - 1u] == ' ')
        --length;
    if (length != wanted || wanted == 0u)
        return false;
    for (uint32_t index = 0u; index < length; ++index)
        if (toupper(text[index]) != toupper(identifier[index]))
            return false;
    return true;
}

/**
 * @brief Whether @p table is an SSDT the dump holds whole, which Load may bring in.
 */
static bool recorded_machine_is_whole_ssdt(const RecordedTable_t *table)
{
    return table->length >= RECORDED_MACHINE_HEADER_BYTES && memcmp(table->signature, "SSDT", 4u) == 0 &&
           recorded_machine_read_field(table, 4u, 4u) == table->length;
}

/**
 * @brief The SSDT of @p machine, not placed yet, that a reference to @p length bytes labelled with the
 *        @p label_length characters at @p label names: one of that length whose OEM table ID the label
 *        names, else the only one of that length; NULL when there is none.
 */
static RecordedTable_t *recorded_machine_named_table(RecordedMachine_t *machine, const uint8_t *label,
                                                     uint32_t label_length, uint64_t length)
{
    RecordedTable_t *only = NULL;
    uint32_t same_length = 0u;

    for (uint32_t index = 0u; index < machine->table_count; ++index)
    {
        RecordedTable_t *table = &machine->tables[index];

        if (!recorded_machine_is_whole_ssdt(table) || table->length != length)
            continue;
        ++same_length;
        only = table;
        if (table->placed == 0u && label != NULL &&
            recorded_machine_names_table(label, label_length, table->bytes + RECORDED_MACHINE_TABLE_IDENTIFIER))
            return table;
    }
    return (same_length == 1u && only->placed == 0u) ? only : NULL;
}

/**
 * @brief Whether @p table holds the bytes of a Load, 5B 20, followed by the @p name_length bytes at
 *        @p name: a Load of that name, or of any when @p name_length is 0.
 */
static bool recorded_machine_holds_load(const RecordedTable_t *table, const uint8_t *name, uint32_t name_length)
{
    for (uint32_t offset = RECORDED_MACHINE_HEADER_BYTES; offset + 2u + name_length <= table->length; ++offset)
        if (table->bytes[offset] == 0x5Bu && table->bytes[offset + 1u] == 0x20u &&
            (name_length == 0u || memcmp(table->bytes + offset + 2u, name, name_length) == 0))
            return true;
    return false;
}

/**
 * @brief Reads, at @p offset of @p source, a reference to a table: a String label followed by an
 *        address and a length, as Intel's PmRef packages hold them, or a SystemMemory OperationRegion
 *        whose address and length are integers and which a Load of @p source names; @p out_label is
 *        NULL for a region.
 *
 * @return false when no reference starts there.
 */
static bool recorded_machine_read_reference(const RecordedTable_t *source, uint32_t offset, const uint8_t **out_label,
                                            uint32_t *out_label_length, uint64_t *out_address, uint64_t *out_length)
{
    const uint8_t *bytes = source->bytes;
    uint32_t cursor = offset + 1u;

    *out_label = NULL;
    *out_label_length = 0u;
    if (bytes[offset] == RECORDED_MACHINE_STRING_PREFIX)
    {
        while (cursor < source->length && cursor - offset <= 9u && bytes[cursor] != 0u)
            ++cursor;
        if (cursor == source->length || bytes[cursor] != 0u)
            return false;
        *out_label = bytes + offset + 1u;
        *out_label_length = cursor - offset - 1u;
        ++cursor;
    }
    else if (bytes[offset] == 0x5Bu && offset + 7u < source->length && bytes[offset + 1u] == 0x80u &&
             (isupper(bytes[offset + 2u]) || bytes[offset + 2u] == '_') && bytes[offset + 6u] == 0x00u &&
             recorded_machine_holds_load(source, bytes + offset + 2u, 4u))
        cursor = offset + 7u;
    else
        return false;
    return recorded_machine_read_integer(bytes, source->length, &cursor, out_address) &&
           recorded_machine_read_integer(bytes, source->length, &cursor, out_length);
}

/**
 * @brief Serves, from the references of @p source, each SSDT they name at the address they give, the
 *        first reference found; a table whose place is taken stays in the XSDT.
 */
static void recorded_machine_place_from(RecordedMachine_t *machine, const RecordedTable_t *source)
{
    for (uint32_t offset = RECORDED_MACHINE_HEADER_BYTES; offset < source->length; ++offset)
    {
        const uint8_t *label = NULL;
        uint32_t label_length = 0u;
        uint64_t address = 0u;
        uint64_t length = 0u;
        RecordedTable_t *table = NULL;

        if (!recorded_machine_read_reference(source, offset, &label, &label_length, &address, &length) ||
            address == 0u || length < RECORDED_MACHINE_HEADER_BYTES || address + length > RECORDED_MACHINE_MAX_PLACE ||
            (table = recorded_machine_named_table(machine, label, label_length, length)) == NULL || table == source)
            continue;
        if (advanced_configuration_and_power_interface_simulated_host_place(&machine->simulated, address, table->bytes,
                                                                            table->length))
        {
            table->placed = address;
            ++machine->placed_count;
        }
    }
}

/**
 * @brief Serves the SSDTs the DSDT and SSDTs of @p machine that hold a Load reference, in the dump's
 *        order, where they reference them, for Load (§8.4).
 */
static void recorded_machine_place_loaded(RecordedMachine_t *machine)
{
    for (uint32_t index = 0u; index < machine->table_count; ++index)
    {
        const RecordedTable_t *source = &machine->tables[index];

        if (source->length >= RECORDED_MACHINE_HEADER_BYTES &&
            (memcmp(source->signature, "SSDT", 4u) == 0 || memcmp(source->signature, "DSDT", 4u) == 0) &&
            recorded_machine_holds_load(source, NULL, 0u))
            recorded_machine_place_from(machine, source);
    }
}

/**
 * @brief Serves the tables of @p machine: the DSDT where each FADT names it, the FACS where the FADT
 *        names it, the SSDTs a triple places where it places them, every other one from
 *        RECORDED_MACHINE_FIRST_ADDRESS, then the root pointer and the XSDT that list those.
 */
static bool recorded_machine_serve(RecordedMachine_t *machine, const char **error)
{
    RecordedTable_t *dsdt = recorded_machine_find(machine, "DSDT");
    uint64_t reserved[2u * RECORDED_MACHINE_MAX_TABLES];
    uint32_t reserved_count = 0u;

    if (!recorded_machine_serve_dsdt(machine, dsdt, reserved, &reserved_count, error) ||
        !recorded_machine_serve_facs(machine, reserved, &reserved_count, error))
        return false;
    recorded_machine_place_loaded(machine);

    uint64_t entries[RECORDED_MACHINE_MAX_TABLES];
    uint32_t entry_count = 0u;
    uint64_t cursor = RECORDED_MACHINE_FIRST_ADDRESS;
    const uint64_t dsdt_length = dsdt != NULL ? dsdt->length : 0u;

    for (uint32_t index = 0u; index < machine->table_count; ++index)
    {
        RecordedTable_t *table = &machine->tables[index];

        if (table == dsdt || table->placed != 0u || recorded_machine_is_left_out(table))
            continue;

        const uint64_t address =
            recorded_machine_free_address(machine, cursor, table->length, reserved, reserved_count, dsdt_length);

        if (!advanced_configuration_and_power_interface_simulated_host_place(&machine->simulated, address, table->bytes,
                                                                             table->length))
        {
            *error = "a table cannot be served";
            return false;
        }
        entries[entry_count++] = address;
        cursor = address + table->length;
    }

    const uint32_t root_length = RECORDED_MACHINE_ROOT_POINTER_BYTES +
                                 (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t) +
                                 entry_count * (uint32_t) sizeof(uint64_t);
    const uint64_t root_address =
        recorded_machine_free_address(machine, cursor, root_length, reserved, reserved_count, dsdt_length);

    machine->root = calloc(1u, root_length);
    if (machine->root == NULL)
    {
        *error = "no memory for the root tables";
        return false;
    }
    recorded_machine_write_root(machine->root, root_address, entries, entry_count);
    if (!advanced_configuration_and_power_interface_simulated_host_place(&machine->simulated, root_address,
                                                                         machine->root, root_length))
    {
        *error = "the root tables cannot be served";
        return false;
    }
    machine->simulated.root_system_description_pointer = root_address;
    return true;
}

/**
 * @brief The first table of @p machine whose bytes are not as many as its header says, written in
 *        @p message, or NULL when each one is whole.
 */
static const char *recorded_machine_find_cut_table(const RecordedMachine_t *machine, char *message, size_t capacity)
{
    for (uint32_t index = 0u; index < machine->table_count; ++index)
    {
        const RecordedTable_t *table = &machine->tables[index];
        const uint64_t declared = recorded_machine_read_field(table, 4u, 4u);

        if (declared != table->length)
        {
            snprintf(message, capacity, "%.4s: %u bytes printed, its header says %llu",
                     (table->length >= 4u) ? table->signature : "????", table->length, (unsigned long long) declared);
            return message;
        }
    }
    return NULL;
}

/**
 * @brief Reads a dump, or the tables a kernel printed, and serves them; a table a kernel printed must
 *        hold as many bytes as its header says, where a dump's may be cut short.
 */
static bool recorded_machine_open_file(RecordedMachine_t *machine, const char *path, bool printed, const char **error)
{
    static char cut[128];

    memset(machine, 0, sizeof(*machine));
    advanced_configuration_and_power_interface_simulated_host_open(&machine->simulated);
    machine->simulated.configuration_fill = machine->simulated.memory_fill;
    if (!recorded_machine_read(machine, path, printed, error))
        return false;

    for (uint32_t index = 0u; index < machine->table_count; ++index)
    {
        RecordedTable_t *table = &machine->tables[index];

        if (table->length >= 4u)
            memcpy(table->signature, table->bytes, sizeof(table->signature));
    }
    if (printed && (*error = recorded_machine_find_cut_table(machine, cut, sizeof(cut))) != NULL)
        return false;
    return recorded_machine_serve(machine, error);
}

/**
 * @brief @p length rounded up to ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_ALIGNMENT.
 */
static uint64_t recorded_machine_padded(uint64_t length)
{
    return (length + ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_ALIGNMENT - 1u) &
           ~(uint64_t) (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_ALIGNMENT - 1u);
}

/**
 * @brief Writes the image of what @p simulated serves into @p image, zeroed and long enough: the
 *        header, then each range in the order it was placed, padded.
 */
static void recorded_machine_fill_image(const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                        uint8_t *image)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHostImageHeader_t header;
    uint64_t offset = sizeof(header);

    memcpy(header.magic, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, sizeof(header.magic));
    header.root_system_description_pointer = simulated->root_system_description_pointer;
    header.range_count = simulated->range_count;
    memcpy(image, &header, sizeof(header));
    for (uint32_t index = 0u; index < simulated->range_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedHostImageRange_t range = {
            simulated->ranges[index].physical_address, simulated->ranges[index].length};

        memcpy(image + offset, &range, sizeof(range));
        offset += sizeof(range);
        memcpy(image + offset, simulated->ranges[index].bytes, (size_t) range.length);
        offset += recorded_machine_padded(range.length);
    }
}

/**
 * @brief Reads @p image back into a simulated host of its own, as ring 0 reads it, and compares what
 *        it serves with what @p simulated serves.
 *
 * @return NULL when the root pointer and every range, its address, length and bytes, are the same,
 *         else what differs.
 */
static const char *
recorded_machine_image_differs(const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                               const uint8_t *image, uint64_t length)
{
    static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t read_back;
    const char *refused = NULL;

    if (!advanced_configuration_and_power_interface_simulated_host_open_image(&read_back, image, length, &refused))
        return refused;
    if (read_back.root_system_description_pointer != simulated->root_system_description_pointer ||
        read_back.range_count != simulated->range_count)
        return "the image read back gives another root pointer or another number of ranges";
    for (uint32_t index = 0u; index < simulated->range_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedRange_t *written = &simulated->ranges[index];
        const AdvancedConfigurationAndPowerInterfaceSimulatedRange_t *read = &read_back.ranges[index];

        if (read->physical_address != written->physical_address || read->length != written->length ||
            memcmp(read->bytes, written->bytes, (size_t) written->length) != 0)
            return "a range of the image read back differs from the one written";
    }
    return NULL;
}

bool recorded_machine_open(RecordedMachine_t *machine, const char *path, const char **error)
{
    return recorded_machine_open_file(machine, path, false, error);
}

bool recorded_machine_open_printed(RecordedMachine_t *machine, const char *path, const char **error)
{
    return recorded_machine_open_file(machine, path, true, error);
}

bool recorded_machine_write_image(const RecordedMachine_t *machine, const char *path, const char **error)
{
    const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = &machine->simulated;
    uint64_t length = sizeof(AdvancedConfigurationAndPowerInterfaceSimulatedHostImageHeader_t);

    for (uint32_t index = 0u; index < simulated->range_count; ++index)
        length += sizeof(AdvancedConfigurationAndPowerInterfaceSimulatedHostImageRange_t) +
                  recorded_machine_padded(simulated->ranges[index].length);

    uint8_t *image = calloc(1u, (size_t) length);

    if (image == NULL)
    {
        *error = "no memory for the image";
        return false;
    }
    recorded_machine_fill_image(simulated, image);

    const char *wrong = recorded_machine_image_differs(simulated, image, length);
    FILE *file = (wrong == NULL) ? fopen(path, "wb") : NULL;

    if (wrong == NULL && file == NULL)
        wrong = "the image cannot be created";
    if (file != NULL)
    {
        const bool whole = (fwrite(image, 1u, (size_t) length, file) == (size_t) length);

        if (fclose(file) != 0 || !whole)
            wrong = "the image cannot be written";
    }
    free(image);
    if (wrong != NULL)
        *error = wrong;
    return wrong == NULL;
}

void recorded_machine_close(RecordedMachine_t *machine)
{
    advanced_configuration_and_power_interface_simulated_host_close(&machine->simulated);
    for (uint32_t index = 0u; index < machine->table_count; ++index)
        free(machine->tables[index].bytes);
    free(machine->root);
    memset(machine, 0, sizeof(*machine));
}
