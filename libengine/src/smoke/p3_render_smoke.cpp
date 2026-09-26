#include "libengine/libengine.h"

#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/render/kernel/KernelDisplayRenderer.hpp>

namespace {

/** Clear colour of the renderer's background. */
constexpr lpl::core::u32 kBackground = 0x00001040u;

/** A colour the renderer never paints, written straight into the surface to test read-back. */
constexpr lpl::core::u32 kDirectProbe = 0x00ABCDEFu;

/**
 * @brief Runs the fixed-timestep loop: each frame is one tick, a clear, then a rasterise.
 *
 * @details The tick advances the Fixed32 rotation angle, which is the deterministic authority;
 *          the clear paints the background and the end of the frame rasterises the triangle and
 *          presents it.
 *
 * @param renderer The renderer under test.
 * @param frames   Frames to render.
 * @return Frames rendered.
 */
lpl::core::u32 renderFrames(lpl::render::kernel::KernelDisplayRenderer &renderer, lpl::core::u32 frames)
{
    lpl::core::u32 rendered = 0u;
    for (lpl::core::u32 frame = 0u; frame < frames; ++frame)
    {
        renderer.tick();
        renderer.beginFrame();
        renderer.endFrame();
        ++rendered;
    }
    return rendered;
}

/**
 * @brief Writes a known pixel straight into the surface and reads it back through the HAL.
 *
 * @details The second stage of the pixel check. When the probe does not come back the HAL
 *          read-back itself is broken, and the recorded centre pixel is replaced by
 *          `0xDEAD0000 | what came back` so the serial line says so; when it does, a centre
 *          left at the background colour means the renderer simply did not paint it.
 *
 * @param platform The kernel platform whose display is probed.
 * @param cx       Column of the probed pixel.
 * @param cy       Row of the probed pixel.
 * @param out      Its centre pixel is overwritten when the read-back fails.
 */
void probeDisplayReadBack(lpl::platform::kernel::KernelPlatform &platform, lpl::core::u32 cx, lpl::core::u32 cy,
                          libengine_p3_render_smoke_result_t *out)
{
    lpl::platform::SurfaceDescriptor probe;
    if (!platform.display().querySurface(probe) || !probe.buffer)
        return;

    probe.buffer[cy * (probe.pitch / 4u) + cx] = kDirectProbe;
    const lpl::core::u32 direct_back = platform.display().readPixel(cx, cy);
    if (direct_back != kDirectProbe)
        out->centre_pixel_raw = 0xDEAD0000u | direct_back;
}

} // namespace

extern "C" void libengine_p3_render_smoke(libengine_p3_render_smoke_result_t *out)
{
    using namespace lpl;
    using lpl::core::u32;

    if (out == nullptr)
        return;

    *out = libengine_p3_render_smoke_result_t{};

    platform::kernel::KernelPlatform platform;
    render::kernel::KernelDisplayRenderer renderer{platform.display()};

    platform::SurfaceDescriptor surface;
    if (!platform.display().querySurface(surface))
        return;

    out->display_available = 1u;

    auto result = renderer.init(surface.width, surface.height);
    (void) result;
    out->renderer_init_ok = 1u;

    constexpr u32 kFrames = 5u;
    platform::IClockBackend &clock = platform.clock();

    const u32 t0 = clock.tickCount();
    out->frames_rendered = renderFrames(renderer, kFrames);
    const u32 t1 = clock.tickCount();
    out->ticks_elapsed = t1 - t0;

    const u32 cx = surface.width / 2u;
    const u32 cy = surface.height / 2u;

    const u32 centre_after_render = platform.display().readPixel(cx, cy);
    out->centre_pixel_raw = centre_after_render;
    out->triangle_visible = (centre_after_render != kBackground) ? 1u : 0u;

    probeDisplayReadBack(platform, cx, cy, out);

    out->smoke_ok = (out->renderer_init_ok && (out->frames_rendered == kFrames) && out->triangle_visible) ? 1u : 0u;
}
