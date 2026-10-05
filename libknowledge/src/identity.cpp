#if !__has_include(<lplknowledge/config.h>)
#    error                                                                                                             \
        "This kernel needs an LplKnowledge with include/lplknowledge/config.h: update ../LplKnowledge, or build with LPLKNOWLEDGE_ROOT=none"
#endif
#include <lplknowledge/config.h>

#include "libknowledge/libknowledge.h"

extern "C" const char *libknowledge_identity_telemetry(void)
{
    return "[LPLTLM] memory version=" LPLKNOWLEDGE_VERSION_STRING " commit=" LPLKNOWLEDGE_COMMIT "\n";
}
