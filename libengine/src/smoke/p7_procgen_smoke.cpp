/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Procedural world parity fold — the cross-target determinism gate for
** generated worlds.
**
** Bakes the canonical recipe (lpl::procgen::parityWorldRecipe) into a real ECS
** registry and folds what it produced. The Linux oracle
** tests/parity/test_world_recipe.cpp bakes the same recipe from the same
** constexpr definition; a bit-for-bit match proves that procedural generation —
** value noise, both erosion models, depression filling and drainage, river
** carving, the climate, Whittaker classification, blue-noise scatter, a cellular
** cave and a settlement — is as deterministic across targets as the physics
** simulation already is.
**
** Nothing here is float-dependent: the noise is pure Q16.16 with no libm, every
** relaxation visits cells in a fixed order, the priority flood breaks ties by
** cell index so a plateau resolves the same way everywhere, and the folds consume
** raw integer words rather than any decimal rendering of them.
*/
#include "libengine/libengine.h"

#include <lpl/ecs/Registry.hpp>
#include <lpl/pack/GamePack.hpp>
#include <lpl/pack/ParityPackBlob.hpp>
#include <lpl/pack/RecipeCodec.hpp>
#include <lpl/procgen/WorldRecipe.hpp>

extern "C" void libengine_procgen_fold_from(const void *pack_bytes, lpl::core::u32 pack_size,
                                            libengine_procgen_fold_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_procgen_fold_result_t{};

    // Function-local static, deliberately NOT a namespace-scope global: a
    // Registry allocates in its constructor, and namespace-scope constructors
    // run from init_array, which the kernel executes BEFORE kmalloc has a heap.
    // A local static defers construction to the first call — this smoke runs
    // from the boot battery, long after the heap is up. Its storage still lives
    // in .bss rather than on the kernel stack, which a Registry would overflow
    // (the same reason sim_fold_smoke.cpp keeps its buffers there).
    static lpl::ecs::Registry world;

    // The recipe is DECODED from a baked pack image, never taken from the
    // compiled-in constexpr: the reader, its bounds checks, its content hash
    // and the wire layout are the shipping ones on both paths.
    //
    // A cartridge (a GRUB boot module) is preferred when the caller supplies
    // one; otherwise the reference pack compiled into the image is used, which
    // is what keeps the parity gate meaningful on a boot with no cartridge.
    // A cartridge that fails to validate is NOT silently replaced by the
    // built-in one: a corrupt game must be reported, not papered over.
    const lpl::core::u8 *bytes = lpl::pack::kParityPackBytes;
    lpl::core::u32 size = lpl::pack::kParityPackSize;

    if (pack_bytes != nullptr && pack_size != 0u)
    {
        bytes = static_cast<const lpl::core::u8 *>(pack_bytes);
        size = pack_size;
        out->from_cartridge = 1u;
    }

    lpl::pack::View view;
    if (!view.open(bytes, size))
        return;

    lpl::pack::RecipeV1 wire{};
    if (!view.readRecipe(wire))
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
