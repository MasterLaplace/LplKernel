#include <kernel/config.h>
#include <lplknowledge/config.h>

#include "libknowledge/libknowledge.h"

extern "C" const char *libknowledge_identity_telemetry(void)
{
    return "[LPLTLM] memory version=" LPLKNOWLEDGE_VERSION_STRING " commit=" LPLKNOWLEDGE_COMMIT "\n";
}
