/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Kernel client entry point — the freestanding mirror of
** LplPlugin/apps/client/main.cpp.
**
** This is the whole kernel<->engine seam: build a Config, construct an Engine
** with a KernelPlatform and the game World, init/run/shutdown. The only
** difference from the hosted client is the injected platform (KernelPlatform
** over the HAL, instead of LinuxPlatform over GLFW/chrono) and the profile
** flags. The kernel holds no renderer, scene, ECS or game logic whatsoever.
**
** Exposed to the C kernel through one extern "C" symbol, because kernel.c is C
** and this translation unit must be built with the engine's C++23 + SSE
** determinism flags.
*/
#include <lpl/core/Log.hpp>
#include <lpl/engine/Config.hpp>
#include <lpl/engine/Engine.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/samples/CubePileWorld.hpp>
#include <lpl/std/memory.hpp>

#include "libengine/libengine.h"

extern "C" void libengine_client_app_run(void)
{
    static lpl::platform::kernel::KernelLogger logger;
    lpl::core::Log::setLogger(&logger);

    lpl::core::Log::info("=== LplKernel Client ===");

    // Budgets are sized for the kernel's 4 MiB heap, not a desktop's. The hosted
    // client's defaults (a 64 MiB arena, 65536 world cells) would exhaust it
    // during Engine::init and starve the simulation of its entity chunks.
    // Which engine built-in system groups this World gets. Physics is the real
    // engine::systems::PhysicsSystem (the same one legacy's client registered
    // via core.registerSystem(Systems::PhysicsSystem()), and the one the server
    // profile uses) — not a bespoke per-game system: CubePileWorld no longer
    // steps its own CpuPhysicsBackend by hand, it just contributes Position/
    // Velocity/AABB/Mass entities to the World's registry like any other game
    // would. Rendering and networking stay off: CubePileWorld does its own
    // rasterizing, and there is no network in ring 0.
    auto config = lpl::engine::Config::Builder{}
                      .tickRate(60)
                      .serverMode(false)
                      .headless(false)
                      .arenaSize(256u * 1024u)
                      // Persistent ECS storage for the World's 1024 cubes:
                      // 4 chunks x (Position+Velocity+AABB+Mass) x 2 buffers.
                      // Bounded here so a World can never eat the 4 MiB heap.
                      .worldArenaSize(512u * 1024u)
                      .worldCellCapacity(1024u)
                      .enablePhysics(true)
                      .enableNetworking(false)
                      .enableRendering(false)
                      .enableGpu(false)
                      .enableBci(false)
                      // Runs the authoritative tick inside a real-time section.
                      // There the heap still serves its bounded O(1) paths (a
                      // slab hit, a TLSF block) and refuses only the unbounded
                      // ones (pool growth, first-fit walk), so a threatened
                      // deadline is caught rather than assumed. Measured on this
                      // profile: 0 unbounded and ~4.5 bounded operations per
                      // step, which holds the deadline without being zero yet.
                      .enableRealTimeGuard(true)
                      .build();

    lpl::engine::Engine engine{config, lpl::pmr::make_unique<lpl::platform::kernel::KernelPlatform>(),
                               lpl::pmr::make_unique<lpl::samples::CubePileWorld>()};

    if (auto result = engine.init(); !result)
    {
        lpl::core::Log::error("Kernel client init failed");
        return;
    }

    engine.run();
    engine.shutdown();

    lpl::core::Log::info("Kernel client exited cleanly");
}
