#include "libengine/libengine.h"

#include <lpl/procgen/Chunking.hpp>

extern "C" void libengine_endless_fold(libengine_endless_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_endless_fold_result_t{};

    const lpl::procgen::EndlessFoldResult folded = lpl::procgen::foldEndlessPatch(
        lpl::procgen::parityChunkParams(), lpl::procgen::parityRiverParams(), lpl::procgen::kParityPatchRadius);

    out->height_sig = folded.heightSignature;
    out->river_sig = folded.riverSignature;
    out->chunks = folded.chunks;
    out->river_cells = folded.riverCells;
    out->seam_mismatches = folded.seamMismatches;
}
