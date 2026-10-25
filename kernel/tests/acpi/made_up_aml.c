#include "made_up_aml.h"

#include <string.h>

/**
 * @brief Appends one byte, or marks the builder overflowed.
 */
static void made_up_aml_byte(MadeUpAml_t *aml, uint8_t byte)
{
    if (aml->length == MADE_UP_AML_CAPACITY)
    {
        aml->overflowed = true;
        return;
    }
    aml->bytes[aml->length++] = byte;
}

/**
 * @brief Appends a value in the PkgLength encoding: @p value itself for a field unit's bits, or, when
 *        @p counts_itself holds, the bytes after it plus its own.
 */
static void made_up_aml_package_length(MadeUpAml_t *aml, uint32_t value, bool counts_itself)
{
    static const uint32_t largest[] = {0x3Fu, 0xFFFu, 0xFFFFFu, 0xFFFFFFFu};
    uint32_t bytes = 1u;

    while (bytes < 4u && value + (counts_itself ? bytes : 0u) > largest[bytes - 1u])
        ++bytes;

    const uint32_t total = value + (counts_itself ? bytes : 0u);

    if (bytes == 1u)
    {
        made_up_aml_byte(aml, (uint8_t) total);
        return;
    }
    made_up_aml_byte(aml, (uint8_t) (((bytes - 1u) << 6u) | (total & 0x0Fu)));
    for (uint32_t index = 1u; index < bytes; ++index)
        made_up_aml_byte(aml, (uint8_t) (total >> (4u + 8u * (index - 1u))));
}

/**
 * @brief Appends a construct: @p lead opcode bytes, a PkgLength, then the @p length bytes of @p body.
 */
static void made_up_aml_package(MadeUpAml_t *aml, const uint8_t *lead, uint32_t lead_length, const uint8_t *body,
                                uint32_t length)
{
    made_up_aml_raw(aml, lead, lead_length);
    made_up_aml_package_length(aml, length, true);
    made_up_aml_raw(aml, body, length);
}

/**
 * @brief Appends a field list's units to @p list.
 */
static void made_up_aml_units(MadeUpAml_t *list, const MadeUpFieldUnit_t *units, uint32_t count)
{
    for (uint32_t index = 0u; index < count; ++index)
    {
        if (units[index].name != NULL)
            made_up_aml_raw(list, (const uint8_t *) units[index].name, 4u);
        else
            made_up_aml_byte(list, 0x00u);
        made_up_aml_package_length(list, units[index].bits, false);
    }
}

void made_up_aml_begin(MadeUpAml_t *aml)
{
    aml->length = 0u;
    aml->overflowed = false;
}

void made_up_aml_integer(MadeUpAml_t *aml, uint64_t value)
{
    const uint32_t bytes = (value <= 0xFFu) ? 1u : (value <= 0xFFFFu) ? 2u : (value <= 0xFFFFFFFFu) ? 4u : 8u;

    if (value <= 1u || value == ~0ull)
    {
        made_up_aml_byte(aml, (value == ~0ull) ? 0xFFu : (uint8_t) value);
        return;
    }
    made_up_aml_byte(aml, (bytes == 1u) ? 0x0Au : (bytes == 2u) ? 0x0Bu : (bytes == 4u) ? 0x0Cu : 0x0Eu);
    for (uint32_t index = 0u; index < bytes; ++index)
        made_up_aml_byte(aml, (uint8_t) (value >> (8u * index)));
}

void made_up_aml_name(MadeUpAml_t *aml, const char *name) { made_up_aml_raw(aml, (const uint8_t *) name, 4u); }

void made_up_aml_name_integer(MadeUpAml_t *aml, const char *name, uint64_t value)
{
    made_up_aml_byte(aml, 0x08u);
    made_up_aml_name(aml, name);
    made_up_aml_integer(aml, value);
}

void made_up_aml_region(MadeUpAml_t *aml, const char *name, uint8_t space, uint64_t offset, uint64_t length)
{
    made_up_aml_byte(aml, 0x5Bu);
    made_up_aml_byte(aml, 0x80u);
    made_up_aml_name(aml, name);
    made_up_aml_byte(aml, space);
    made_up_aml_integer(aml, offset);
    made_up_aml_integer(aml, length);
}

void made_up_aml_field(MadeUpAml_t *aml, const char *region, uint8_t flags, const MadeUpFieldUnit_t *units,
                       uint32_t count)
{
    static const uint8_t lead[] = {0x5Bu, 0x81u};
    MadeUpAml_t body;

    made_up_aml_begin(&body);
    made_up_aml_name(&body, region);
    made_up_aml_byte(&body, flags);
    made_up_aml_units(&body, units, count);
    aml->overflowed = aml->overflowed || body.overflowed;
    made_up_aml_package(aml, lead, sizeof(lead), body.bytes, body.length);
}

void made_up_aml_index_field(MadeUpAml_t *aml, const char *index, const char *data, uint8_t flags,
                             const MadeUpFieldUnit_t *units, uint32_t count)
{
    static const uint8_t lead[] = {0x5Bu, 0x86u};
    MadeUpAml_t body;

    made_up_aml_begin(&body);
    made_up_aml_name(&body, index);
    made_up_aml_name(&body, data);
    made_up_aml_byte(&body, flags);
    made_up_aml_units(&body, units, count);
    aml->overflowed = aml->overflowed || body.overflowed;
    made_up_aml_package(aml, lead, sizeof(lead), body.bytes, body.length);
}

void made_up_aml_bank_field(MadeUpAml_t *aml, const char *region, const char *bank, uint64_t bank_value, uint8_t flags,
                            const MadeUpFieldUnit_t *units, uint32_t count)
{
    static const uint8_t lead[] = {0x5Bu, 0x87u};
    MadeUpAml_t body;

    made_up_aml_begin(&body);
    made_up_aml_name(&body, region);
    made_up_aml_name(&body, bank);
    made_up_aml_integer(&body, bank_value);
    made_up_aml_byte(&body, flags);
    made_up_aml_units(&body, units, count);
    aml->overflowed = aml->overflowed || body.overflowed;
    made_up_aml_package(aml, lead, sizeof(lead), body.bytes, body.length);
}

void made_up_aml_store_name(MadeUpAml_t *aml, const char *source, const char *target)
{
    made_up_aml_byte(aml, 0x70u);
    made_up_aml_name(aml, source);
    made_up_aml_name(aml, target);
}

void made_up_aml_store_integer(MadeUpAml_t *aml, uint64_t value, const char *target)
{
    made_up_aml_byte(aml, 0x70u);
    made_up_aml_integer(aml, value);
    made_up_aml_name(aml, target);
}

void made_up_aml_raw(MadeUpAml_t *aml, const uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index)
        made_up_aml_byte(aml, bytes[index]);
}
