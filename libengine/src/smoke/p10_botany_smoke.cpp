/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Botany parity fold — the fourth gate: the SHAPE a grammar grows.
**
** P7 folds the world a recipe builds, P8 the simulation that runs on it, P9 the
** world that streams. None of them exercises the 3D turtle in procgen/Botany.cpp,
** whose every branch endpoint comes out of a CORDIC rotation — the same primitive
** the camera basis and the terrain noise are built on. A tree is scenery, but the
** arithmetic that grows it is not, and this is the cheapest place to catch a
** CORDIC that behaves differently on i686.
**
** Must match tests/parity/test_botany_parity.cpp on the host, bit for bit.
*/
#include "libengine/libengine.h"

#include <lpl/procgen/Botany.hpp>

extern "C" void libengine_botany_fold(libengine_botany_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_botany_fold_result_t{};

    // parityTreeParams is the one definition both sides read, so a parameter that
    // moves moves both sides or fails to compile — the rule the endless gate
    // learned the hard way.
    const lpl::procgen::TreeSkeleton conifer =
        lpl::procgen::growTree(lpl::procgen::parityTreeParams(lpl::procgen::TreeSpecies::Conifer));
    const lpl::procgen::TreeSkeleton broadleaf =
        lpl::procgen::growTree(lpl::procgen::parityTreeParams(lpl::procgen::TreeSpecies::Broadleaf));
    const lpl::procgen::TreeSkeleton shrub =
        lpl::procgen::growTree(lpl::procgen::parityTreeParams(lpl::procgen::TreeSpecies::Shrub));

    out->conifer_sig = lpl::procgen::foldTreeSkeleton(conifer);
    out->broadleaf_sig = lpl::procgen::foldTreeSkeleton(broadleaf);
    out->shrub_sig = lpl::procgen::foldTreeSkeleton(shrub);
    out->conifer_segments = static_cast<uint32_t>(conifer.branches.size());
    out->conifer_leaves = static_cast<uint32_t>(conifer.leaves.size());
}
