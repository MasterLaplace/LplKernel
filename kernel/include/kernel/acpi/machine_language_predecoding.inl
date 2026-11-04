#include <stdbool.h>
#include <stdint.h>

static inline bool advanced_configuration_and_power_interface_machine_language_record_writes(uint8_t shape)
{
    return shape == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_STORE ||
           shape == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_INCREMENT ||
           shape == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_DECREMENT;
}

static inline uint16_t advanced_configuration_and_power_interface_machine_language_record_recent(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t *cache, uint16_t table, uint32_t pc)
{
    const uint16_t index =
        cache->recent[pc % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECENT_RECORDS];

    if (index == 0u || index > cache->record_count || cache->records[index - 1u].pc != pc ||
        cache->records[index - 1u].table != table)
        return 0u;
    return index;
}
