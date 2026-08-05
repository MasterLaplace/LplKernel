/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Rosetta parity fold — the artifact that carries the specification of its reader.
**
** A cartridge assumes a reader compiled from this repository. A Rosetta plate assumes
** nothing but an observer: the instruction set is engraved beside the payload, five
** copies of the bootstrap sit at the corners and the centre, and a fifth of the area
** is transversal parity.
**
** What this gate folds is not "the plate exists" but the claim that makes it an
** artifact rather than a blob: a machine REBUILT from the engraved bytes runs the
** canonical program to the same trace as the compiled-in one. If ring 0 disagreed with
** the host about that, the specification would not be sufficient on every target,
** which is the only property a stranger could ever rely on.
**
** Must match tests/parity/test_rosetta_isa.cpp on the host, bit for bit.
*/
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
