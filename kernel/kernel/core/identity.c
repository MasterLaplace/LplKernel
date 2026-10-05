#include <kernel/config.h>
#include <kernel/core/identity.h>

const char *kernel_identity_configuration(void) { return KERNEL_CONFIG_STRING; }

const char *kernel_identity_telemetry(void)
{
    return "[LPLTLM] build version=" KERNEL_VERSION_STRING "+" KERNEL_BUILD " commit=" KERNEL_COMMIT "\n";
}
