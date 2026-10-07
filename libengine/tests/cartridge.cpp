#include <kernel/boot/boot_module.h>

#include <lpl/ecs/Registry.hpp>
#include <lpl/pack/GamePack.hpp>
#include <lpl/pack/RecipeCodec.hpp>
#include <lpl/procgen/WorldRecipe.hpp>
#include <lpl/std/memory.hpp>
#include <lpl/testing/Test.hpp>

LPL_TEST_SUITE(cartridge);

namespace {

/**
 * @brief Bakes @p recipe into a registry of its own, on the heap: the kernel stack cannot hold one,
 *        and a bake folds everything its registry holds.
 */
[[nodiscard]] lpl::procgen::WorldRecipeResult bake(const lpl::procgen::WorldRecipe &recipe)
{
    const auto registry = lpl::pmr::make_unique<lpl::ecs::Registry>();

    return lpl::procgen::bakeWorld(*registry, recipe);
}

} // namespace

/**
 * @brief The game pack GRUB loads beside the kernel, read by the reader that ships, bakes the
 *        parity world.
 *
 * @details The module is looked up as `game.lplpak`: the image also carries `world.lplpak`, the
 *          viewer's, and matching the extension alone would bake whichever GRUB listed first.
 */
LPL_TEST(boot_module_bakes_the_parity_world)
{
    const uint8_t *bytes = nullptr;
    uint32_t size = 0u;
    lpl::pack::View view;
    lpl::pack::RecipeV1 wire{};

    if (!boot_module_find("game.lplpak", &bytes, &size))
    {
        test.skip("no game pack among the boot modules");
        return;
    }
    if (!test.check(view.open(bytes, size) && view.readRecipe(wire), "the cartridge opens and its recipe decodes"))
        return;

    const lpl::procgen::WorldRecipeResult cartridge = bake(lpl::pack::toEngineRecipe(wire));
    const lpl::procgen::WorldRecipeResult reference = bake(lpl::procgen::parityWorldRecipe());

    test.check(cartridge == reference, "it bakes the parity world, field for field");
    test.check(cartridge.ok == 1u, "which passes its own gate");
}
