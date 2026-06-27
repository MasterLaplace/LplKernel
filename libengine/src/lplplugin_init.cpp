/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** lplplugin_initialize — the extern "C" engine boot facade.
**
** Single real entry point the kernel calls from kernel_main once the heap, PCI,
** clock and framebuffer HAL are up. Constructs a KernelPlatform (the four HAL
** backends) and the KernelDisplayRenderer, then drives the engine's own
** lpl::engine::GameLoop (fixed-timestep, paced by the platform IClockBackend)
** with render callbacks targeting the IDisplayBackend. This is the in-kernel
** sibling of a hosted main() that builds and runs the LplPlugin engine, and it
** exercises the freestanding GameLoop end to end.
*/
#include "libengine/libengine.h"

#include <lpl/engine/Config.hpp>
#include <lpl/engine/GameLoop.hpp>
#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/render/kernel/KernelDisplayRenderer.hpp>

extern "C" int lplplugin_initialize(const lplplugin_boot_info_t *boot, lplplugin_boot_result_t *out)
{
    using namespace lpl;
    using lpl::core::u32;

    lplplugin_boot_result_t result{};

    // ABI gate: refuse to boot against an unknown boot_info layout.
    if (boot == nullptr || boot->abi_version != LPLPLUGIN_BOOT_ABI_VERSION)
    {
        if (out != nullptr)
            *out = result;
        return 1;
    }
    result.abi_ok = 1u;

    // -----------------------------------------------------------------------
    // Platform (HAL backends) + renderer.
    // -----------------------------------------------------------------------
    platform::kernel::KernelPlatform platform;
    result.platform_ok = 1u;

    render::kernel::KernelDisplayRenderer renderer{platform.display()};

    platform::SurfaceDescriptor surface;
    if (!platform.display().querySurface(surface))
    {
        // No framebuffer (text-mode boot): a clean headless no-op boot.
        if (out != nullptr)
            *out = result;
        return 0;
    }
    result.display_available = 1u;

    auto initResult = renderer.init(surface.width, surface.height);
    (void) initResult;
    result.renderer_init_ok = 1u;

    // -----------------------------------------------------------------------
    // Engine game loop. The fixed timestep advances the Fixed32 rotation
    // authority; the render callback rasterises + presents one frame. The
    // IClockBackend paces wall time only (never the Fixed32 authority).
    // max_frames == 0 runs until shutdown (a real boot owns the loop); any
    // positive value renders that many frames then requests stop.
    // -----------------------------------------------------------------------
    const engine::Config config = engine::Config::Builder{}.build();
    engine::GameLoop loop{config, platform.clock()};

    const u32 maxFrames = boot->max_frames;
    u32 framesRendered = 0u;

    engine::LoopCallbacks callbacks;
    callbacks.fixedUpdate = [&renderer](core::f64 /*dt*/) { renderer.tick(); };
    callbacks.render = [&renderer, &framesRendered, &loop, maxFrames](core::f64 /*alpha*/) {
        renderer.beginFrame();
        renderer.endFrame();
        ++framesRendered;
        if (maxFrames != 0u && framesRendered >= maxFrames)
            loop.requestStop();
    };

    loop.run(callbacks);
    result.frames_rendered = framesRendered;

    renderer.shutdown();
    result.shutdown_clean = 1u;

    if (out != nullptr)
        *out = result;
    return 0;
}
