#if !__has_include(<lplassistant/config.h>)
#    error "This kernel needs lplassistant/config.h: update ../LplAssistant, or set LPLASSISTANT_ROOT=none"
#endif
#include <lplassistant/config.h>

#include "libassistant/libassistant.h"

extern "C" const char *libassistant_identity_telemetry(void)
{
    return "[LPLTLM] mind version=" LPLASSISTANT_VERSION_STRING " commit=" LPLASSISTANT_COMMIT "\n";
}
