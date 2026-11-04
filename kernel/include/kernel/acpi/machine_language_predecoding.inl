#include <stdbool.h>
#include <stdint.h>

static inline bool advanced_configuration_and_power_interface_machine_language_record_writes(uint8_t shape)
{
    return shape == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_STORE ||
           shape == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_INCREMENT ||
           shape == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_DECREMENT;
}
