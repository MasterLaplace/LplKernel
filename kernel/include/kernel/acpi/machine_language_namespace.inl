#include <stdbool.h>
#include <stdint.h>

static inline bool advanced_configuration_and_power_interface_machine_language_namespace_is_device_object(uint8_t type)
{
    return type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE ||
           type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PROCESSOR ||
           type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_THERMAL_ZONE;
}
