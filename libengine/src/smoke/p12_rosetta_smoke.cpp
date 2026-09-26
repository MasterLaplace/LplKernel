#include "libengine/libengine.h"

#include <lpl/rosetta/Parity.hpp>

extern "C" void libengine_rosetta_fold(libengine_rosetta_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_rosetta_fold_result_t{};

    lpl::rosetta::RosettaFoldResult folded{};
    lpl::rosetta::foldRosettaState(folded);

    out->trace_sig = folded.traceSignature;
    out->spec_sig = folded.specSignature;
    out->plate_sig = folded.plateSignature;
    out->payload_sig = folded.payloadSignature;
    out->steps = folded.steps;
    out->halted = folded.halted;
    out->plate_bytes = folded.plateBytes;
    out->rebuilt_opcodes = folded.rebuiltOpcodes;
    out->self_hosting = folded.selfHosting;
}
