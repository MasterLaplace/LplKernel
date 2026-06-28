/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P5 render present smoke — rasterizes the depth-buffered 3D cube
** (lpl::render::SoftwareRasterizer) into an offscreen buffer and presents a
** scaled copy onto the display scanout through the HAL (IDisplayBackend). The
** 96x64 offscreen fold must match the Linux oracle (tests/test-render-parity
** cube angle0 sig) bit-for-bit: geometry/rotation is Fixed32/CORDIC, the
** projection + barycentric fill is float (SSE, -ffp-contract=off).
*/
#include "libengine/libengine.h"

#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/render/SoftwareRasterizer.hpp>
#include <lpl/render/Texture.hpp>

namespace {
// Offscreen render targets in BSS (kept off the kernel stack/heap).
constexpr lpl::core::u32 kSigW = 96u;
constexpr lpl::core::u32 kSigH = 64u;
lpl::core::u32 g_sigColor[kSigW * kSigH];
lpl::core::f32 g_sigDepth[kSigW * kSigH];

constexpr lpl::core::u32 kMvW = 128u;
constexpr lpl::core::u32 kMvH = 96u;
lpl::core::u32 g_mvColor[kMvW * kMvH];
lpl::core::f32 g_mvDepth[kMvW * kMvH];

constexpr lpl::core::u32 kVisW = 320u;
constexpr lpl::core::u32 kVisH = 240u;
lpl::core::u32 g_visColor[kVisW * kVisH];
lpl::core::f32 g_visDepth[kVisW * kVisH];
} // namespace

extern "C" void libengine_p5_render_present_smoke(libengine_p5_render_present_result_t *out)
{
    using namespace lpl;
    using lpl::core::u32;

    if (out == nullptr)
        return;
    *out = libengine_p5_render_present_result_t{};

    // Offscreen folds (must match the oracle's 96x64 cube signatures).
    const auto checker = render::Texture::makeChecker(64u, 64u, 0x00FF0000u, 0x000000FFu, 8u);
    render::RenderTarget sigTarget{g_sigColor, g_sigDepth, kSigW, kSigH};
    render::renderCube(sigTarget, math::Fixed32::fromInt(0));
    out->cube_signature = render::foldTarget(sigTarget);
    render::renderTexturedCube(sigTarget, math::Fixed32::fromInt(0), checker);
    out->textured_cube_sig = render::foldTarget(sigTarget);
    render::renderLitCube(sigTarget, math::Fixed32::fromInt(0), render::ShadingModel::BlinnPhong);
    out->lit_cube_sig = render::foldTarget(sigTarget);

    // Multi-viewport (128x96) and render-to-texture (96x64) folds.
    render::RenderTarget mvTarget{g_mvColor, g_mvDepth, kMvW, kMvH};
    render::renderMultiViewport(mvTarget);
    out->multiviewport_sig = render::foldTarget(mvTarget);
    render::renderToTextureCube(sigTarget, math::Fixed32::fromInt(0));
    out->rtt_sig = render::foldTarget(sigTarget);

    platform::kernel::KernelPlatform platformBackends;
    platform::IDisplayBackend &display = platformBackends.display();

    platform::SurfaceDescriptor surface;
    if (!display.querySurface(surface) || surface.buffer == nullptr || surface.width == 0u || surface.height == 0u)
        return; // no surface (text-mode boot) -> visual present skipped

    out->display_available = 1u;
    out->width = surface.width;
    out->height = surface.height;

    // Render a higher-resolution 2x2 multi-viewport composite for the present.
    render::RenderTarget visTarget{g_visColor, g_visDepth, kVisW, kVisH};
    render::renderMultiViewport(visTarget);

    // Nearest-neighbour upscale into the real surface (pitch-aware).
    const u32 pitchPixels = (surface.pitch != 0u ? surface.pitch : surface.width * 4u) / 4u;
    for (u32 y = 0u; y < surface.height; ++y)
    {
        const u32 sy = (y * kVisH) / surface.height;
        core::u32 *dstRow = surface.buffer + y * pitchPixels;
        const core::u32 *srcRow = g_visColor + sy * kVisW;
        for (u32 x = 0u; x < surface.width; ++x)
            dstRow[x] = srcRow[(x * kVisW) / surface.width];
    }
    display.present();

    out->present_ok = 1u;
}
