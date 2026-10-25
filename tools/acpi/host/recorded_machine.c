#include "recorded_machine.h"

#include <kernel/acpi/tables.h>

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
 * @brief Reads the tables of the dump at @p path.
 */
static bool recorded_machine_read(RecordedMachine_t *machine, const char *path, const char **error)
{
    FILE *file = fopen(path, "r");

    if (file == NULL)
    {
        *error = "the dump cannot be opened";
        return false;
    }

    char line[512];
    RecordedTable_t *table = NULL;
    bool fits = true;

    while (fits && fgets(line, sizeof(line), file) != NULL)
    {
        if (recorded_machine_is_header(line))
        {
            fits = machine->table_count < RECORDED_MACHINE_MAX_TABLES;
            table = fits ? &machine->tables[machine->table_count++] : NULL;
        }
        else if (table != NULL)
        {
            (void) recorded_machine_read_row(table, line);
        }
    }
    fclose(file);
    if (!fits)
        *error = "the dump holds more tables than a recorded machine";
    else if (machine->table_count == 0u)
        *error = "the dump holds no table";
    return fits && machine->table_count != 0u;
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
 *        @p reserved_count ranges at @p reserved, each @p reserved_length bytes long.
 */
static uint64_t recorded_machine_free_address(uint64_t cursor, uint64_t length, const uint64_t *reserved,
                                              uint32_t reserved_count, uint64_t reserved_length)
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
 * @brief Serves the tables of @p machine: the DSDT where each FADT names it, the FACS where the FADT
 *        names it, every other one from RECORDED_MACHINE_FIRST_ADDRESS, then the root pointer and the
 *        XSDT that list them.
 */
static bool recorded_machine_serve(RecordedMachine_t *machine, const char **error)
{
    RecordedTable_t *dsdt = recorded_machine_find(machine, "DSDT");
    uint64_t reserved[2u * RECORDED_MACHINE_MAX_TABLES];
    uint32_t reserved_count = 0u;

    if (!recorded_machine_serve_dsdt(machine, dsdt, reserved, &reserved_count, error) ||
        !recorded_machine_serve_facs(machine, reserved, &reserved_count, error))
        return false;

    uint64_t entries[RECORDED_MACHINE_MAX_TABLES];
    uint32_t entry_count = 0u;
    uint64_t cursor = RECORDED_MACHINE_FIRST_ADDRESS;
    const uint64_t dsdt_length = dsdt != NULL ? dsdt->length : 0u;

    for (uint32_t index = 0u; index < machine->table_count; ++index)
    {
        RecordedTable_t *table = &machine->tables[index];

        if (table == dsdt || recorded_machine_is_left_out(table))
            continue;

        const uint64_t address =
            recorded_machine_free_address(cursor, table->length, reserved, reserved_count, dsdt_length);

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
        recorded_machine_free_address(cursor, root_length, reserved, reserved_count, dsdt_length);

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

bool recorded_machine_open(RecordedMachine_t *machine, const char *path, const char **error)
{
    memset(machine, 0, sizeof(*machine));
    advanced_configuration_and_power_interface_simulated_host_open(&machine->simulated);
    if (!recorded_machine_read(machine, path, error))
        return false;

    for (uint32_t index = 0u; index < machine->table_count; ++index)
    {
        RecordedTable_t *table = &machine->tables[index];

        if (table->length >= 4u)
            memcpy(table->signature, table->bytes, sizeof(table->signature));
    }
    return recorded_machine_serve(machine, error);
}

void recorded_machine_close(RecordedMachine_t *machine)
{
    for (uint32_t index = 0u; index < machine->table_count; ++index)
        free(machine->tables[index].bytes);
    free(machine->root);
    memset(machine, 0, sizeof(*machine));
}
