/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Kernel server entry point — the freestanding mirror of
** LplPlugin/apps/server/main.cpp.
**
** Same shape as the client entry point, and for the same reason: everything that
** was host-independent (budgets, engine construction, the loop) is in
** engine::bootGame and engine::HostProfile. What is left is the platform seam, the
** tick rate a server wants, and which World it hosts.
**
** Exposed to the C kernel through one extern "C" symbol.
*/
#include <lpl/core/Log.hpp>
#include <lpl/engine/Boot.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/samples/CubePileWorld.hpp>
#include <lpl/std/memory.hpp>

#include "libengine/libengine.h"

extern "C" void libengine_server_app_run(void)
{
    static lpl::platform::kernel::KernelLogger logger;
    lpl::core::Log::setLogger(&logger);

    lpl::engine::BootRequest request;
    request.host = lpl::engine::HostProfile::Ring0Server;
    // 144 Hz, and it is not decoration: the deterministic tick is what the parity
    // gate folds, and the server profile is the one that runs it flat out.
    request.tickRate = 144u;
    request.banner = "=== LplKernel Server ===";

    lpl::engine::bootGame(
        request, lpl::pmr::make_unique<lpl::platform::kernel::KernelPlatform>(),
        [](const lpl::procgen::WorldRecipe &, const lpl::ecology::LivingRecipe &, const lpl::engine::ViewProfile &) {
            return lpl::pmr::unique_ptr<lpl::engine::World>{lpl::pmr::make_unique<lpl::samples::CubePileWorld>()};
        },
        // The one budget a server states for itself: ten thousand entities, which
        // the ring-0 profile's memory ceiling does not otherwise imply.
        [](lpl::engine::Config::Builder &builder) { builder.maxEntities(10000u); });
}
