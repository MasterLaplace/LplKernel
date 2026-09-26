#include "libengine/libengine.h"

#include <lpl/engine/ReliefParity.hpp>

extern "C" void libengine_relief_fold(libengine_relief_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_relief_fold_result_t{};

    const lpl::engine::ReliefFoldResult measured = lpl::engine::foldReliefParity();
    const lpl::engine::ReliefFoldResult invented = lpl::engine::foldInventedParity();

    out->sample_sig = measured.sampleSignature;
    out->height_sig = measured.heightSignature;
    out->walk_sig = measured.walkSignature;
    out->coast_sig = measured.coastSignature;
    out->measured = measured.measuredCells;
    out->invented = measured.inventedCells;
    out->blended = measured.blendedCells;
    out->sea = measured.seaCells;
    out->steps = measured.walkSteps;
    out->descended = measured.descended;
    out->plain_height = invented.heightSignature;
    out->plain_walk = invented.walkSignature;
}
