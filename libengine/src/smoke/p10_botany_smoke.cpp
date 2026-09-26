#include "libengine/libengine.h"

#include <lpl/procgen/Botany.hpp>

extern "C" void libengine_botany_fold(libengine_botany_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_botany_fold_result_t{};

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
