/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Endless-world parity fold — the determinism gate for the world that STREAMS.
**
** P7 folds the world a recipe builds, P8 folds the simulation that runs on it.
** Neither covers the chunked world: its terrain is sampled at absolute
** coordinates and its rivers are decided by a bounded basin plus a coarse trunk
** level, all of it verified on the host and assumed on the target. This runs the
** same patch the Linux oracle runs (tests/parity/test_procgen_chunking.cpp) and
** folds it.
**
** Nothing here is float-dependent in the folded path: the noise is pure Q16.16
** with no libm, the flow walks compare Fixed32 and break ties on the neighbour
** index, and the folds consume raw integer words.
*/
#include "libengine/libengine.h"

#include <lpl/procgen/Chunking.hpp>

extern "C" void libengine_endless_fold(libengine_endless_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_endless_fold_result_t{};

    // The one constexpr, shared with the oracle. These parameters used to be a
    // copy here and a copy in the test, kept in step by review — the only gate of
    // the three that relied on someone remembering. Now a parameter that moves
    // moves both sides, or fails to compile.
    const lpl::procgen::EndlessFoldResult folded = lpl::procgen::foldEndlessPatch(
        lpl::procgen::parityChunkParams(), lpl::procgen::parityRiverParams(), lpl::procgen::kParityPatchRadius);

    out->height_sig = folded.heightSignature;
    out->river_sig = folded.riverSignature;
    out->chunks = folded.chunks;
    out->river_cells = folded.riverCells;
    out->seam_mismatches = folded.seamMismatches;
}
