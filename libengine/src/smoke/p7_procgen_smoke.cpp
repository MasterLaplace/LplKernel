#include "libengine/libengine.h"

#include <lpl/ecs/Registry.hpp>
#include <lpl/pack/GamePack.hpp>
#include <lpl/pack/ParityPackBlob.hpp>
#include <lpl/pack/RecipeCodec.hpp>
#include <lpl/procgen/WorldRecipe.hpp>

namespace {

/**
 * @brief The registry the world is baked into.
 *
 * @note A function-local static, deliberately NOT a namespace-scope global: a Registry
 *       allocates in its constructor, and namespace-scope constructors run from init_array,
 *       which the kernel executes BEFORE kmalloc has a heap. A local static defers
 *       construction to the first call — this smoke runs from the boot battery, long after
 *       the heap is up. Its storage still lives in .bss rather than on the kernel stack,
 *       which a Registry would overflow (the same reason sim_fold_smoke.cpp keeps its
 *       buffers there).
 *
 * @return The registry.
 */
lpl::ecs::Registry &worldRegistry()
{
    static lpl::ecs::Registry world;
    return world;
}

/**
 * @brief Decodes the recipe from a baked pack image: the cartridge when one was given, the
 *        reference pack compiled into the image otherwise.
 *
 * @details The recipe is never taken from the compiled-in constexpr: the reader, its bounds
 *          checks, its content hash and the wire layout are the shipping ones on both paths.
 *          The reference pack is what keeps the parity gate meaningful on a boot with no
 *          cartridge.
 *
 * @note A cartridge that fails to validate is NOT silently replaced by the built-in one: a
 *       corrupt game must be reported, not papered over.
 *
 * @param pack_bytes Cartridge image, or NULL for the reference pack.
 * @param pack_size  Its size in bytes; 0 selects the reference pack too.
 * @param wire       Receives the decoded recipe.
 * @param out        Its from_cartridge flag says which pack was read.
 * @return true when the pack opened and the recipe decoded.
 */
bool readRecipe(const void *pack_bytes, lpl::core::u32 pack_size, lpl::pack::RecipeV1 &wire,
                libengine_procgen_fold_result_t *out)
{
    const lpl::core::u8 *bytes = lpl::pack::kParityPackBytes;
    lpl::core::u32 size = lpl::pack::kParityPackSize;

    if (pack_bytes != nullptr && pack_size != 0u)
    {
        bytes = static_cast<const lpl::core::u8 *>(pack_bytes);
        size = pack_size;
        out->from_cartridge = 1u;
    }

    lpl::pack::View view;
    return view.open(bytes, size) && view.readRecipe(wire);
}

} // namespace

extern "C" void libengine_procgen_fold_from(const void *pack_bytes, lpl::core::u32 pack_size,
                                            libengine_procgen_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_procgen_fold_result_t{};

    lpl::ecs::Registry &world = worldRegistry();

    lpl::pack::RecipeV1 wire{};
    if (!readRecipe(pack_bytes, pack_size, wire, out))
        return;

    out->pack_ok = 1u;

    const lpl::procgen::WorldRecipeResult baked = lpl::procgen::bakeWorld(world, lpl::pack::toEngineRecipe(wire));

    out->entity_count = baked.entityCount;
    out->state_sig = baked.stateSignature;
    out->height_sig = baked.heightSignature;
    out->biome_sig = baked.biomeSignature;
    out->river_cells = baked.riverCells;
    out->road_cells = baked.roadCells;
    out->lake_cells = baked.lakeCells;
    out->cave_floor = baked.dungeonFloor;
    out->plots = baked.settlementPlots;
    out->gate_reachable = baked.gateReachable;
    out->gate_visited = baked.gateVisited;
    out->gate_path_length = baked.gatePathLength;
    out->world_ok = baked.ok;
}

extern "C" void libengine_procgen_fold(libengine_procgen_fold_result_t *out)
{
    libengine_procgen_fold_from(nullptr, 0u, out);
}
