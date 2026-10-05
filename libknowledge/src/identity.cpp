#if !__has_include(<lplknowledge/config.h>)
#    error "This kernel needs lplknowledge/config.h: update ../LplKnowledge, or set LPLKNOWLEDGE_ROOT=none"
#endif
#include <lplknowledge/config.h>

#include "libknowledge/libknowledge.h"

extern "C" const char *libknowledge_identity_telemetry(void)
{
    return "[LPLTLM] memory version=" LPLKNOWLEDGE_VERSION_STRING " commit=" LPLKNOWLEDGE_COMMIT "\n";
}
