/*
** Gate P21: standing on measured ground in ring 0.
**
** Folds the same world the Linux oracle folds, plus the control that ignores the
** survey. The counts travel with the signatures because a stable signature cannot
** tell a working field from one that never applied.
**
** Must match tests/parity/test_relief_parity.cpp.
*/
#include "libengine/libengine.h"

#include <lpl/engine/ReliefParity.hpp>

extern "C" void libengine_relief_fold(libengine_relief_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_relief_fold_result_t{};

    const lpl::engine::ReliefFoldResult measured = lpl::engine::foldReliefParity();
    // The control, folded alongside rather than behind a second entry point: the two runs
    // differ in one thing, and a caller able to fetch one without the other could report
    // the survey working without reporting that it changed anything.
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
