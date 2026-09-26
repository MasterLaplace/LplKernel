#include "libengine/libengine.h"

#include <lpl/render/Lighting.hpp>
#include <lpl/render/RenderParity.hpp>
#include <lpl/render/Texture.hpp>

namespace {

/**
 * @brief Texture sampling determinism: folds a diagonal of bilinear samples of a checker.
 * @return The FNV-1a fold of 64 samples along the diagonal.
 */
lpl::core::u32 foldBilinearDiagonal()
{
    const auto tex = lpl::render::Texture::makeChecker(64u, 64u, 0x00FF0000u, 0x000000FFu, 8u);
    lpl::core::u32 texSig = 0x811C9DC5u;
    for (lpl::core::u32 i = 0; i < 64u; ++i)
    {
        const lpl::core::u32 uq = (i * 65536u) / 64u;
        texSig = lpl::render::detail::fnv1aStep(texSig, tex.sampleBilinear(uq, uq));
    }
    return texSig;
}

/**
 * @brief Classical lighting of a reference fragment under one directional light.
 * @param out Receives the Lambert and Blinn-Phong colours.
 */
void shadeReferenceFragment(libengine_p5_render_smoke_result_t *out)
{
    lpl::render::Material mat;
    mat.albedo = lpl::render::Vec3f(0.8f, 0.7f, 0.6f);
    mat.shininess = 32u;
    lpl::render::Light dir;
    dir.type = lpl::render::LightType::Directional;
    dir.direction = lpl::render::Vec3f(-0.4f, -0.7f, -0.6f);
    const lpl::render::Vec3f N(0.0f, 0.0f, 1.0f);
    const lpl::render::Vec3f frag(0.0f, 0.0f, 1.0f);
    const lpl::render::Vec3f eye(0.0f, 0.0f, 5.0f);
    out->lambert_rgb = lpl::render::shadeToRgb(lpl::render::ShadingModel::Lambert, mat, &dir, 1u, N, frag, eye);
    out->blinn_rgb = lpl::render::shadeToRgb(lpl::render::ShadingModel::BlinnPhong, mat, &dir, 1u, N, frag, eye);
}

} // namespace

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

    out->tex_sample_sig = foldBilinearDiagonal();
    shadeReferenceFragment(out);

    out->render_ok = (r0.in_front_count == 8u && rq.in_front_count == 8u &&
                      rq.screen_signature != r0.screen_signature && r0.vertex0_x > 0 && r0.vertex0_x < 1280 &&
                      r0.vertex0_y > 0 && r0.vertex0_y < 800 && cull.total == 49u && cull.visible > 0u &&
                      cull.visible < cull.total)
                         ? 1u
                         : 0u;
}
