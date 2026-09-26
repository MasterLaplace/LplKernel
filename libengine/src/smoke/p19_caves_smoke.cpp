#include "libengine/libengine.h"

#include <lpl/engine/CaveParity.hpp>

extern "C" void libengine_caves_fold(libengine_caves_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_caves_fold_result_t{};

    const lpl::engine::CaveFoldResult open = lpl::engine::foldCaveParity();
    const lpl::engine::CaveFoldResult sealed = lpl::engine::foldSealedCaveParity();

    out->warren_sig = open.warrenSignature;
    out->walk_sig = open.walkSignature;
    out->span_sig = open.spanSignature;
    out->sealed_sig = sealed.walkSignature;
    out->covered = open.coveredColumns;
    out->open_cells = open.openCells;
    out->reachable = open.reachableCells;
    out->aperture = open.apertureCells;
    out->path = open.pathLength;
    out->enclosed = open.enclosedTicks;
    out->descended = open.descendedLevels;
    out->sealed_in = sealed.enclosedTicks;
    out->navigable = open.navigable;
    out->kind = open.kind;
}
