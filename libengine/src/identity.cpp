#include <kernel/config.h>
#include <lpl/config.h>

#include "libengine/libengine.h"

extern "C" const char *libengine_identity_telemetry(void)
{
    return "[LPLTLM] engine version=" LPLPLUGIN_VERSION_STRING " commit=" LPLPLUGIN_COMMIT "\n";
}
