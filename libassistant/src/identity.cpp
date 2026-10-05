#include <kernel/config.h>
#include <lplassistant/config.h>

#include "libassistant/libassistant.h"

extern "C" const char *libassistant_identity_telemetry(void)
{
    return "[LPLTLM] mind version=" LPLASSISTANT_VERSION_STRING " commit=" LPLASSISTANT_COMMIT "\n";
}
