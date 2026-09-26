#include <lpl/core/Log.hpp>
#include <lpl/engine/Boot.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/samples/CubePileWorld.hpp>
#include <lpl/std/memory.hpp>

#include "libengine/libengine.h"

namespace {

/**
 * The server's tick, in hertz. Not decoration: the deterministic tick is what the parity gate
 * folds, and the server profile is the one that runs it flat out.
 */
constexpr lpl::core::u32 kServerTickRate = 144u;

/**
 * @brief Builds the World the server profile runs: the cube pile.
 * @return The World.
 */
lpl::pmr::unique_ptr<lpl::engine::World>
makeServerWorld(const lpl::procgen::WorldRecipe &, const lpl::ecology::LivingRecipe &, const lpl::engine::ViewProfile &)
{
    return lpl::pmr::unique_ptr<lpl::engine::World>{lpl::pmr::make_unique<lpl::samples::CubePileWorld>()};
}

/**
 * @brief The one budget a server states for itself: ten thousand entities, which the ring-0
 *        profile's memory ceiling does not otherwise imply.
 * @param builder The configuration being built.
 */
void stateServerBudget(lpl::engine::Config::Builder &builder) { builder.maxEntities(10000u); }

} // namespace

extern "C" void libengine_server_app_run(void)
{
    static lpl::platform::kernel::KernelLogger logger;
    lpl::core::Log::setLogger(&logger);

    lpl::engine::BootRequest request;
    request.host = lpl::engine::HostProfile::Ring0Server;
    request.tickRate = kServerTickRate;
    request.banner = "=== LplKernel Server ===";

    lpl::engine::bootGame(request, lpl::pmr::make_unique<lpl::platform::kernel::KernelPlatform>(), makeServerWorld,
                          stateServerBudget);
}
