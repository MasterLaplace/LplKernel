/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Cave parity fold — the determinism gate for a cave you can walk into.
**
** Every gate before this one folds a world that was GENERATED. This one folds a
** world that was generated and then WALKED, because the claim being made is not
** that two targets build the same cave but that a body entering it ends up in the
** same place on both. Three links, each Fixed32 and each able to disagree on its
** own: the warren, the vertical span query that turns a column into a floor and a
** ceiling, and the character controller that decides where a body may stand.
**
** Nothing here is float-dependent in the folded path: the volume is voxels, the
** cover mask is bytes, the span is raw Q16.16 words and the body's fold consumes
** its own raw state. Must match tests/parity/test_cave_warren.cpp.
*/
#include "libengine/libengine.h"

#include <lpl/engine/CaveParity.hpp>

extern "C" void libengine_caves_fold(libengine_caves_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_caves_fold_result_t{};

    const lpl::engine::CaveFoldResult open = lpl::engine::foldCaveParity();
    // The control, folded alongside rather than in a second entry point: the two runs
    // differ in one thing, and a caller that could fetch one without the other would
    // be able to report the cave working without reporting that rock stops anybody.
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
