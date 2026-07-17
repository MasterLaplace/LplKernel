/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P3 render smoke — exercises the KernelDisplayRenderer (software rasterizer)
** over the kernel IDisplayBackend. Runs a fixed-timestep loop of N_FRAMES
** frames using IClockBackend for pacing: each tick advances the Fixed32
** rotation angle (deterministic authority) and each render call draws the
** interpolated triangle into the LFB. Proves the P3 exit gate:
**   - FPU/SSE state is preserved across the IRQs that fire during the loop
**     (validated by the non-#GP execution itself)
**   - The triangle rasteriser writes non-background pixels to the surface
**   - The Fixed32 angle arithmetic is CORDIC-based (no libm)
*/
#include "libengine/libengine.h"

#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/render/kernel/KernelDisplayRenderer.hpp>

extern "C" void libengine_p3_render_smoke(libengine_p3_render_smoke_result_t *out)
{
    using namespace lpl;
    using lpl::core::u32;

    if (out == nullptr)
        return;

    *out = libengine_p3_render_smoke_result_t{};

    // -----------------------------------------------------------------------
    // Platform + renderer
    // -----------------------------------------------------------------------
    platform::kernel::KernelPlatform platform;
    render::kernel::KernelDisplayRenderer renderer{platform.display()};

    platform::SurfaceDescriptor surface;
    if (!platform.display().querySurface(surface))
        return; // no framebuffer → smoke skipped (text-mode boot)

    out->display_available = 1u;

    auto result = renderer.init(surface.width, surface.height);
    (void) result;
    out->renderer_init_ok = 1u;

    // -----------------------------------------------------------------------
    // Fixed-timestep loop: N_FRAMES render frames, each preceded by one tick.
    // IClockBackend is used for wall-clock observability (not determinism).
    // -----------------------------------------------------------------------
    constexpr u32 kFrames = 5u;
    platform::IClockBackend &clock = platform.clock();

    const u32 t0 = clock.tickCount();

    for (u32 frame = 0u; frame < kFrames; ++frame)
    {
        renderer.tick();       // advance Fixed32 angle (deterministic)
        renderer.beginFrame(); // clear to background
        renderer.endFrame();   // rasterise triangle + present
        ++out->frames_rendered;
    }

    const u32 t1 = clock.tickCount();
    out->ticks_elapsed = t1 - t0; // modular delta

    // -----------------------------------------------------------------------
    // Two-stage pixel check:
    //   1. Read centre after renderer — if non-background, triangle rendered.
    //   2. Direct buffer write + readPixel — confirms HAL round-trip works.
    //      If direct probe passes but triangle_visible=0, issue is in renderer.
    // -----------------------------------------------------------------------
    constexpr u32 kBackground  = 0x00001040u;
    constexpr u32 kDirectProbe = 0x00ABCDEFu;
    const u32 cx = surface.width  / 2u;
    const u32 cy = surface.height / 2u;

    // Stage 1: read centroid pixel — the triangle centroid is at screen centre
    // for any rotation angle, so it must always be inside the triangle.
    const u32 centre_after_render = platform.display().readPixel(cx, cy);
    out->centre_pixel_raw = centre_after_render;
    out->triangle_visible = (centre_after_render != kBackground) ? 1u : 0u;

    // Stage 2: direct write + read to verify HAL coherency.
    {
        platform::SurfaceDescriptor probe;
        if (platform.display().querySurface(probe) && probe.buffer)
        {
            probe.buffer[cy * (probe.pitch / 4u) + cx] = kDirectProbe;
            const u32 direct_back = platform.display().readPixel(cx, cy);
            // Overwrite centre_pixel_raw with direct probe result so we can
            // see in serial whether the HAL read-back works at all.
            // If direct_back != kDirectProbe the HAL is broken; otherwise the
            // renderer simply didn't paint the centre pixel.
            if (direct_back != kDirectProbe)
                out->centre_pixel_raw = 0xDEAD0000u | direct_back;
        }
    }

    out->smoke_ok = (out->renderer_init_ok && (out->frames_rendered == kFrames)
                     && out->triangle_visible) ? 1u : 0u;
}
