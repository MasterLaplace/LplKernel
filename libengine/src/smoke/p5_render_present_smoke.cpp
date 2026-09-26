#include "libengine/libengine.h"

#include <lpl/platform/kernel/KernelPlatform.hpp>
#include <lpl/render/SoftwareRasterizer.hpp>
#include <lpl/render/Texture.hpp>

namespace {

/**
 * Offscreen render targets, in BSS so they stay off the kernel stack and heap: the
 * signature target (96x64, the oracle's cube size), the multi-viewport target and the
 * higher-resolution target that is presented.
 */
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

/**
 * @brief Renders and folds the offscreen cubes the oracle folds.
 *
 * @details The plain, textured and lit cubes at the oracle's 96x64, then the 128x96
 *          multi-viewport and the 96x64 render-to-texture cube.
 *
 * @param out Receives the five signatures.
 */
void foldOffscreenTargets(libengine_p5_render_present_result_t *out)
{
    const auto checker = lpl::render::Texture::makeChecker(64u, 64u, 0x00FF0000u, 0x000000FFu, 8u);
    lpl::render::RenderTarget sigTarget{g_sigColor, g_sigDepth, kSigW, kSigH};
    lpl::render::renderCube(sigTarget, lpl::math::Fixed32::fromInt(0));
    out->cube_signature = lpl::render::foldTarget(sigTarget);
    lpl::render::renderTexturedCube(sigTarget, lpl::math::Fixed32::fromInt(0), checker);
    out->textured_cube_sig = lpl::render::foldTarget(sigTarget);
    lpl::render::renderLitCube(sigTarget, lpl::math::Fixed32::fromInt(0), lpl::render::ShadingModel::BlinnPhong);
    out->lit_cube_sig = lpl::render::foldTarget(sigTarget);

    lpl::render::RenderTarget mvTarget{g_mvColor, g_mvDepth, kMvW, kMvH};
    lpl::render::renderMultiViewport(mvTarget);
    out->multiviewport_sig = lpl::render::foldTarget(mvTarget);
    lpl::render::renderToTextureCube(sigTarget, lpl::math::Fixed32::fromInt(0));
    out->rtt_sig = lpl::render::foldTarget(sigTarget);
}

/**
 * @brief Copies the presented target onto the real surface, nearest-neighbour and pitch-aware.
 * @param surface The display surface; its buffer receives the scaled image.
 */
void upscaleOntoSurface(const lpl::platform::SurfaceDescriptor &surface)
{
    const lpl::core::u32 pitchPixels = (surface.pitch != 0u ? surface.pitch : surface.width * 4u) / 4u;
    for (lpl::core::u32 y = 0u; y < surface.height; ++y)
    {
        const lpl::core::u32 sy = (y * kVisH) / surface.height;
        lpl::core::u32 *dstRow = surface.buffer + y * pitchPixels;
        const lpl::core::u32 *srcRow = g_visColor + sy * kVisW;
        for (lpl::core::u32 x = 0u; x < surface.width; ++x)
            dstRow[x] = srcRow[(x * kVisW) / surface.width];
    }
}

} // namespace

extern "C" void libengine_p5_render_present_smoke(libengine_p5_render_present_result_t *out)
{
    using namespace lpl;

    if (out == nullptr)
        return;
    *out = libengine_p5_render_present_result_t{};

    foldOffscreenTargets(out);

    platform::kernel::KernelPlatform platformBackends;
    platform::IDisplayBackend &display = platformBackends.display();

    platform::SurfaceDescriptor surface;
    if (!display.querySurface(surface) || surface.buffer == nullptr || surface.width == 0u || surface.height == 0u)
        return;

    out->display_available = 1u;
    out->width = surface.width;
    out->height = surface.height;

    render::RenderTarget visTarget{g_visColor, g_visDepth, kVisW, kVisH};
    render::renderMultiViewport(visTarget);
    upscaleOntoSurface(surface);
    display.present();

    out->present_ok = 1u;
}
