#include "libengine/libengine.h"

#include <lpl/render/CommandBuffer.hpp>
#include <lpl/render/Foveated.hpp>
#include <lpl/render/Pbr.hpp>
#include <lpl/render/RayTracer.hpp>
#include <lpl/render/Topology.hpp>

namespace {

/** Render targets, in BSS so they stay off the kernel stack and heap. */
constexpr lpl::core::u32 kRtW = 96u;
constexpr lpl::core::u32 kRtH = 72u;
lpl::core::u32 g_rayImg[kRtW * kRtH];

constexpr lpl::core::u32 kFvW = 128u;
constexpr lpl::core::u32 kFvH = 96u;
lpl::core::u32 g_fovImg[kFvW * kFvH];

/**
 * @brief P6.1 topology: a Catmull loop, a saddle, and a Delaunay triangulation.
 * @param out Receives the three signatures and the triangle count.
 * @return true when the sample and triangle counts are the ones the gate expects.
 */
bool foldTopology(libengine_p6_smoke_result_t *out)
{
    using lpl::math::Fixed32;

    const Fixed32 ctrl[5][3] = {
        {Fixed32::fromInt(-2), Fixed32::fromInt(0), Fixed32::fromInt(-2)},
        {Fixed32::fromInt(2), Fixed32::fromInt(0), Fixed32::fromInt(-2)},
        {Fixed32::fromInt(2), Fixed32::fromInt(0), Fixed32::fromInt(2)},
        {Fixed32::fromInt(-2), Fixed32::fromInt(0), Fixed32::fromInt(2)},
        {Fixed32::fromInt(0), Fixed32::fromInt(3), Fixed32::fromInt(0)},
    };
    const auto loop = lpl::render::tessellateCatmullLoop(ctrl, 5u, 8u);
    const auto saddle = lpl::render::tessellateSaddle(16u);
    out->catmull_sig = loop.sample_signature;
    out->saddle_sig = saddle.sample_signature;

    const Fixed32 pts[6][3] = {
        {Fixed32::fromInt(0), Fixed32::fromInt(0), Fixed32::fromInt(0)},
        {Fixed32::fromInt(4), Fixed32::fromInt(0), Fixed32::fromInt(0)},
        {Fixed32::fromInt(4), Fixed32::fromInt(4), Fixed32::fromInt(0)},
        {Fixed32::fromInt(0), Fixed32::fromInt(4), Fixed32::fromInt(0)},
        {Fixed32::fromInt(2), Fixed32::fromInt(2), Fixed32::fromInt(0)},
        {Fixed32::fromInt(1), Fixed32::fromInt(3), Fixed32::fromInt(0)},
    };
    const auto del = lpl::render::delaunay2D(pts, 6u);
    out->delaunay_tris = del.triangle_count;
    out->delaunay_sig = del.triangle_signature;

    return loop.sample_count == 40u && saddle.sample_count == 17u * 17u && del.triangle_count > 0u;
}

/**
 * @brief P6.2 software ray tracing.
 * @param out Receives the hit count and the image signature.
 * @return true when at least one ray hit.
 */
bool foldRayTracing(libengine_p6_smoke_result_t *out)
{
    const auto rt = lpl::render::rayTraceScene(g_rayImg, kRtW, kRtH, 3u);
    out->ray_hits = rt.hit_count;
    out->ray_image_sig = rt.image_signature;
    return rt.hit_count > 0u;
}

/**
 * @brief P6.3 metallic/roughness PBR with an HDRI tone map, on a gold and a plastic material.
 * @param out Receives the three shaded colours.
 */
void foldPhysicallyBasedShading(libengine_p6_smoke_result_t *out)
{
    lpl::render::PbrMaterial gold;
    gold.albedo = lpl::render::Vec3f(1.0f, 0.77f, 0.34f);
    gold.metallic = 1.0f;
    gold.roughness = 0.25f;
    lpl::render::PbrMaterial plastic;
    plastic.albedo = lpl::render::Vec3f(0.2f, 0.6f, 0.9f);
    plastic.metallic = 0.0f;
    plastic.roughness = 0.6f;
    lpl::render::Light key;
    key.type = lpl::render::LightType::Directional;
    key.direction = lpl::render::Vec3f(-0.5f, -0.8f, -0.6f);
    key.intensity = 3.0f;
    const lpl::render::Vec3f N(0.0f, 0.0f, 1.0f);
    const lpl::render::Vec3f frag(0.0f, 0.0f, 0.0f);
    const lpl::render::Vec3f eye(0.0f, 0.0f, 3.0f);
    const lpl::render::Vec3f hdri(0.12f, 0.14f, 0.18f);
    out->pbr_gold_reinhard =
        lpl::render::pbrShadeToRgb(gold, &key, 1u, N, frag, eye, hdri, lpl::render::ToneMap::Reinhard);
    out->pbr_gold_aces = lpl::render::pbrShadeToRgb(gold, &key, 1u, N, frag, eye, hdri, lpl::render::ToneMap::Aces);
    out->pbr_plastic_aces =
        lpl::render::pbrShadeToRgb(plastic, &key, 1u, N, frag, eye, hdri, lpl::render::ToneMap::Aces);
}

/**
 * @brief P6.4 and P6.5: an immutable command buffer, submitted twice with late-latched poses.
 *
 * @details A command recorded after finalize must be refused, and two submissions whose poses
 *          differ by half a unit must latch differently.
 *
 * @param out Receives the recording signature and both latched signatures.
 * @return true when the buffer kept four commands and the two latches differ.
 */
bool foldCommandBuffer(libengine_p6_smoke_result_t *out)
{
    using lpl::math::Fixed32;

    lpl::render::CommandBuffer cb;
    for (lpl::core::u32 i = 0; i < 4u; ++i)
        cb.record(lpl::render::DrawCommand{0x1000u + i * 0x100u, 0x9000u + i * 0x40u, 36u, 1u, i, i & 1u});
    cb.finalize();
    cb.record(lpl::render::DrawCommand{});
    out->cmd_recording_sig = cb.recordingSignature();

    lpl::render::Pose poses[4];
    for (lpl::core::u32 i = 0; i < 4u; ++i)
        poses[i].x = Fixed32::fromInt(static_cast<lpl::core::i32>(i));
    out->cmd_latched0_sig = lpl::render::submitLateLatched(cb, poses, 4u).latched_signature;
    for (lpl::core::u32 i = 0; i < 4u; ++i)
        poses[i].x = poses[i].x + Fixed32::fromFloat(0.5f);
    out->cmd_latched1_sig = lpl::render::submitLateLatched(cb, poses, 4u).latched_signature;

    return cb.count() == 4u && out->cmd_latched0_sig != out->cmd_latched1_sig;
}

/**
 * @brief P6.6 foveated rasterisation.
 * @param out Receives the shaded and full fragment counts and the image signature.
 * @return true when foveation shaded fewer fragments than a full pass would.
 */
bool foldFoveatedRasterisation(libengine_p6_smoke_result_t *out)
{
    const auto fov = lpl::render::foveatedShade(g_fovImg, kFvW, kFvH, 64u, 48u);
    out->foveated_shaded = fov.shaded_fragments;
    out->foveated_full = fov.full_fragments;
    out->foveated_image_sig = fov.image_signature;
    return fov.shaded_fragments < fov.full_fragments;
}

} // namespace

extern "C" void libengine_p6_smoke(libengine_p6_smoke_result_t *out)
{
    if (out == nullptr)
        return;
    *out = libengine_p6_smoke_result_t{};

    const bool topology_ok = foldTopology(out);
    const bool ray_tracing_ok = foldRayTracing(out);
    foldPhysicallyBasedShading(out);
    const bool command_buffer_ok = foldCommandBuffer(out);
    const bool foveation_ok = foldFoveatedRasterisation(out);

    out->p6_ok = (topology_ok && ray_tracing_ok && command_buffer_ok && foveation_ok) ? 1u : 0u;
}
