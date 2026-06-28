/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P5 render smoke — projects a Fixed32-authored unit cube (CORDIC model
** rotation) through a perspective camera and folds the resulting screen
** coordinates + depths inside the kernel. The reported signatures must match
** the Linux oracle (tests/test-render-parity) bit-for-bit: the geometry and
** rotation are authoritative Fixed32/CORDIC, the projection/divide is float
** (SSE, -ffp-contract=off) which P1 proved bit-identical across targets.
*/
#include "libengine/libengine.h"

#include <lpl/render/Lighting.hpp>
#include <lpl/render/RenderParity.hpp>
#include <lpl/render/Texture.hpp>

extern "C" void libengine_p5_render_smoke(libengine_p5_render_smoke_result_t *out)
{
    using namespace lpl;

    if (out == nullptr)
        return;
    *out = libengine_p5_render_smoke_result_t{};

    const auto r0 = render::projectParityCube(math::Fixed32::fromInt(0), 1280u, 800u);
    const auto rq = render::projectParityCube(math::Fixed32::fromFloat(0.78539816f), 1280u, 800u);

    out->angle0_screen_sig = r0.screen_signature;
    out->angle0_depth_sig = r0.depth_signature;
    out->angle0_vertex0_x = r0.vertex0_x;
    out->angle0_vertex0_y = r0.vertex0_y;
    out->angle0_in_front = r0.in_front_count;
    out->quarter_screen_sig = rq.screen_signature;

    const auto cull = render::cullParityInstanceGrid(1280u, 800u);
    out->cull_total = cull.total;
    out->cull_visible = cull.visible;
    out->cull_visible_sig = cull.visible_signature;

    // Texture sampling determinism: fold a diagonal of bilinear samples.
    const auto tex = render::Texture::makeChecker(64u, 64u, 0x00FF0000u, 0x000000FFu, 8u);
    core::u32 texSig = 0x811C9DC5u;
    for (core::u32 i = 0; i < 64u; ++i)
    {
        const core::u32 uq = (i * 65536u) / 64u;
        texSig = render::detail::fnv1aStep(texSig, tex.sampleBilinear(uq, uq));
    }
    out->tex_sample_sig = texSig;

    // Classical lighting of a reference fragment (directional light).
    render::Material mat;
    mat.albedo = render::Vec3f(0.8f, 0.7f, 0.6f);
    mat.shininess = 32u;
    render::Light dir;
    dir.type = render::LightType::Directional;
    dir.direction = render::Vec3f(-0.4f, -0.7f, -0.6f);
    const render::Vec3f N(0.0f, 0.0f, 1.0f);
    const render::Vec3f frag(0.0f, 0.0f, 1.0f);
    const render::Vec3f eye(0.0f, 0.0f, 5.0f);
    out->lambert_rgb = render::shadeToRgb(render::ShadingModel::Lambert, mat, &dir, 1u, N, frag, eye);
    out->blinn_rgb = render::shadeToRgb(render::ShadingModel::BlinnPhong, mat, &dir, 1u, N, frag, eye);

    out->render_ok = (r0.in_front_count == 8u && rq.in_front_count == 8u &&
                      rq.screen_signature != r0.screen_signature && r0.vertex0_x > 0 && r0.vertex0_x < 1280 &&
                      r0.vertex0_y > 0 && r0.vertex0_y < 800 && cull.total == 49u && cull.visible > 0u &&
                      cull.visible < cull.total)
                         ? 1u
                         : 0u;
}
