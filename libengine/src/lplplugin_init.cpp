/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** lplplugin_initialize — the extern "C" engine boot facade.
**
** Single real entry point the kernel calls from kernel_main once the heap, PCI,
** clock and framebuffer HAL are up. Constructs a KernelPlatform (the four HAL
** backends) and the KernelDisplayRenderer, then drives a clock-paced render
** loop over the platform IClockBackend / IDisplayBackend. This is the in-kernel
** sibling of a hosted main() that builds and runs the LplPlugin engine.
**
** The fixed-cadence loop is inlined here for now rather than delegating to
** lpl::engine::GameLoop: GameLoop is freestanding-ready (reparented onto
** IClockBackend, lpl::pmr::function callbacks) but pulling it into libengine.a
** still needs lpl::engine::Config to route <string> through lpl/std. Until that
** lands, this facade owns the loop directly. The structure mirrors GameLoop so
** the swap is mechanical once Config is converted.
*/
#include "libengine/libengine.h"

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
    // Clock-paced render loop. max_frames == 0 means "run until shutdown" (a
    // real boot owns the main loop); any positive value renders exactly that
    // many frames then returns (bounded boot smoke / CI). The IClockBackend is
    // sampled for wall-clock pacing only — it never feeds the Fixed32 authority
    // that drives the triangle rotation.
    // -----------------------------------------------------------------------
    platform::IClockBackend &clock = platform.clock();
    const bool bounded = (boot->max_frames != 0u);

    u32 lastTick = clock.tickCount();
    for (u32 frame = 0u; !bounded || frame < boot->max_frames; ++frame)
    {
        // Pace to the clock: advance one simulation tick per observed clock
        // tick edge so the cadence tracks wall time rather than spinning flat
        // out. On a headless/zero-Hz clock this degrades to free-running.
        if (clock.tickHertz() != 0u)
        {
            u32 now = clock.tickCount();
            while (now == lastTick)
                now = clock.tickCount();
            lastTick = now;
        }

        renderer.tick();       // advance Fixed32 rotation angle (deterministic)
        renderer.beginFrame(); // clear to background
        renderer.endFrame();   // rasterise triangle + present
        ++result.frames_rendered;
    }

    renderer.shutdown();
    result.shutdown_clean = 1u;

    if (out != nullptr)
        *out = result;
    return 0;
}
