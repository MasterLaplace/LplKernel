#include "made_up_table.h"

#include <string.h>

const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *made_up_definition_block(uint8_t *table, const char *signature,
                                                                                  const uint8_t *aml, uint32_t length)
{
    const uint32_t total = MADE_UP_TABLE_HEADER + length;
    uint8_t sum = 0u;

    memset(table, 0, MADE_UP_TABLE_HEADER);
    memcpy(table, signature, 4u);
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        table[4u + byte] = (uint8_t) (total >> (8u * byte));
    table[8] = 2u;
    memcpy(table + 10, "LPLTST", 6u);
    memcpy(table + 16, "MADEUPML", 8u);
    memcpy(table + MADE_UP_TABLE_HEADER, aml, length);
    for (uint32_t byte = 0u; byte < total; ++byte)
        sum = (uint8_t) (sum + table[byte]);
    table[9] = (uint8_t) (0u - sum);
    return (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) table;
}
