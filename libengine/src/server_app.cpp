/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Kernel server entry point — the freestanding mirror of
** LplPlugin/apps/server/main.cpp.
**
** This is the whole kernel<->engine seam: build a Config, construct an Engine
** with a KernelPlatform and the game World, init/run/shutdown. The only
** difference from the hosted server is the injected platform (KernelPlatform
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

extern "C" void libengine_server_app_run(void)
{
    static lpl::platform::kernel::KernelLogger logger;
    lpl::core::Log::setLogger(&logger);

    lpl::core::Log::info("=== LplKernel Server ===");

    auto config = lpl::engine::Config::Builder{}
                      .tickRate(144)
                      .maxEntities(10000)
                      .serverMode(true)
                      .headless(true)
                      .arenaSize(256u * 1024u)
                      .worldCellCapacity(1024u)
                      .enableGpu(false)
                      .enableBci(false)
                      .build();

    lpl::engine::Engine engine{config, lpl::pmr::make_unique<lpl::platform::kernel::KernelPlatform>(),
                               lpl::pmr::make_unique<lpl::samples::CubePileWorld>()};

    if (auto result = engine.init(); !result)
    {
        lpl::core::Log::error("Kernel server init failed");
        return;
    }

    engine.run();
    engine.shutdown();

    lpl::core::Log::info("Kernel server exited cleanly");
}
