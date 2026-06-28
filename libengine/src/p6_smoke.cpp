/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P6 smoke — runs the advanced rendering slices (topology, software ray
** tracing, metallic/roughness PBR + HDRI tone mapping, immutable command
** buffers with Late-Latching, foveated rasterization) entirely in-kernel and
** folds their results. Authoritative geometry/poses are Fixed32; the render
** math is float (SSE, -ffp-contract=off) which P1 proved bit-identical across
** the host and the i686 kernel. Every reported signature must equal the Linux
** oracle (tests/test-p6-parity) bit-for-bit.
*/
#include "libengine/libengine.h"

#include <lpl/render/CommandBuffer.hpp>
#include <lpl/render/Foveated.hpp>
#include <lpl/render/Pbr.hpp>
#include <lpl/render/RayTracer.hpp>
#include <lpl/render/Topology.hpp>

namespace {
// Render targets in BSS (kept off the kernel stack/heap).
constexpr lpl::core::u32 kRtW = 96u;
constexpr lpl::core::u32 kRtH = 72u;
lpl::core::u32 g_rayImg[kRtW * kRtH];

constexpr lpl::core::u32 kFvW = 128u;
constexpr lpl::core::u32 kFvH = 96u;
lpl::core::u32 g_fovImg[kFvW * kFvH];
} // namespace

extern "C" void libengine_p6_smoke(libengine_p6_smoke_result_t *out)
{
    using namespace lpl;
    using math::Fixed32;

    if (out == nullptr)
        return;
    *out = libengine_p6_smoke_result_t{};

    // --- P6.1 topology ---
    const Fixed32 ctrl[5][3] = {
        {Fixed32::fromInt(-2), Fixed32::fromInt(0), Fixed32::fromInt(-2)},
        {Fixed32::fromInt(2), Fixed32::fromInt(0), Fixed32::fromInt(-2)},
        {Fixed32::fromInt(2), Fixed32::fromInt(0), Fixed32::fromInt(2)},
        {Fixed32::fromInt(-2), Fixed32::fromInt(0), Fixed32::fromInt(2)},
        {Fixed32::fromInt(0), Fixed32::fromInt(3), Fixed32::fromInt(0)},
    };
    const auto loop = render::tessellateCatmullLoop(ctrl, 5u, 8u);
    const auto saddle = render::tessellateSaddle(16u);
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
    const auto del = render::delaunay2D(pts, 6u);
    out->delaunay_tris = del.triangle_count;
    out->delaunay_sig = del.triangle_signature;

    // --- P6.2 software ray tracing ---
    const auto rt = render::rayTraceScene(g_rayImg, kRtW, kRtH, 3u);
    out->ray_hits = rt.hit_count;
    out->ray_image_sig = rt.image_signature;

    // --- P6.3 PBR metallic/roughness + HDRI tone map ---
    render::PbrMaterial gold;
    gold.albedo = render::Vec3f(1.0f, 0.77f, 0.34f);
    gold.metallic = 1.0f;
    gold.roughness = 0.25f;
    render::PbrMaterial plastic;
    plastic.albedo = render::Vec3f(0.2f, 0.6f, 0.9f);
    plastic.metallic = 0.0f;
    plastic.roughness = 0.6f;
    render::Light key;
    key.type = render::LightType::Directional;
    key.direction = render::Vec3f(-0.5f, -0.8f, -0.6f);
    key.intensity = 3.0f;
    const render::Vec3f N(0.0f, 0.0f, 1.0f);
    const render::Vec3f frag(0.0f, 0.0f, 0.0f);
    const render::Vec3f eye(0.0f, 0.0f, 3.0f);
    const render::Vec3f hdri(0.12f, 0.14f, 0.18f);
    out->pbr_gold_reinhard = render::pbrShadeToRgb(gold, &key, 1u, N, frag, eye, hdri, render::ToneMap::Reinhard);
    out->pbr_gold_aces = render::pbrShadeToRgb(gold, &key, 1u, N, frag, eye, hdri, render::ToneMap::Aces);
    out->pbr_plastic_aces = render::pbrShadeToRgb(plastic, &key, 1u, N, frag, eye, hdri, render::ToneMap::Aces);

    // --- P6.4/P6.5 command buffer + late-latching ---
    render::CommandBuffer cb;
    for (core::u32 i = 0; i < 4u; ++i)
        cb.record(render::DrawCommand{0x1000u + i * 0x100u, 0x9000u + i * 0x40u, 36u, 1u, i, i & 1u});
    cb.finalize();
    cb.record(render::DrawCommand{});
    out->cmd_recording_sig = cb.recordingSignature();
    render::Pose poses[4];
    for (core::u32 i = 0; i < 4u; ++i)
        poses[i].x = Fixed32::fromInt(static_cast<core::i32>(i));
    out->cmd_latched0_sig = render::submitLateLatched(cb, poses, 4u).latched_signature;
    for (core::u32 i = 0; i < 4u; ++i)
        poses[i].x = poses[i].x + Fixed32::fromFloat(0.5f);
    out->cmd_latched1_sig = render::submitLateLatched(cb, poses, 4u).latched_signature;

    // --- P6.6 foveated rasterizer ---
    const auto fov = render::foveatedShade(g_fovImg, kFvW, kFvH, 64u, 48u);
    out->foveated_shaded = fov.shaded_fragments;
    out->foveated_full = fov.full_fragments;
    out->foveated_image_sig = fov.image_signature;

    out->p6_ok = (loop.sample_count == 40u && saddle.sample_count == 17u * 17u && del.triangle_count > 0u &&
                  rt.hit_count > 0u && cb.count() == 4u &&
                  out->cmd_latched0_sig != out->cmd_latched1_sig && fov.shaded_fragments < fov.full_fragments)
                     ? 1u
                     : 0u;
}
