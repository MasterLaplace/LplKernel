/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Simulation parity fold — the cross-target determinism gate for the active
** sample simulation.
**
** Runs the sim headless for a fixed tick count into an internal target and
** folds authoritative state + rendered image. The kernel prints the signatures
** on serial; the Linux oracle (tests/parity/test_cubepile_parity.cpp) computes
** the SAME values, so a bit-for-bit match proves the freestanding engine build
** is identical to the hosted one (the HARD determinism contract).
**
** This is the only remaining C entry point into the simulation: the live client
** payload is no longer driven through a C facade but by lpl::engine::Engine
** with an injected IApplication (see libengine/src/client_app.cpp). The fold
** stays C-callable because the kernel smoke battery (kernel/core/
** libengine_smoke.c) is C and reports it alongside the P0..P6 gates.
**
** Dimensions must match the oracle for the image fold to agree.
*/
#include "libengine/libengine.h"

#include <lpl/samples/CubePile.hpp>

namespace {
// The simulation under parity. Keep in step with the payload client_app.cpp
// injects, so the gate covers what actually ships.
using ActiveSim = lpl::samples::CubePile;

constexpr lpl::core::u32 kWidth = 192u;
constexpr lpl::core::u32 kHeight = 120u;

// BSS, not stack: a 192x120 colour+depth pair is ~180 KiB, far past the kernel
// stack. Only the fold touches these.
lpl::core::u32 g_color[kWidth * kHeight];
lpl::core::f32 g_depth[kWidth * kHeight];
} // namespace

extern "C" void libengine_sim_fold(libengine_sim_fold_result_t *out)
{
    using namespace lpl;

    if (out == nullptr)
        return;
    *out = libengine_sim_fold_result_t{};

    render::RenderTarget renderTarget{g_color, g_depth, kWidth, kHeight};

    const auto early = samples::runCubePileAndFold(renderTarget, 8u);
    const auto late = samples::runCubePileAndFold(renderTarget, 64u);

    out->state_sig_8 = early.state_signature;
    out->image_sig_8 = early.image_signature;
    out->state_sig_64 = late.state_signature;
    out->image_sig_64 = late.image_signature;
    out->sim_ok = (early.state_signature != late.state_signature &&
                   late.image_signature != render::detail::kFnv1aOffsetBasis)
                      ? 1u
                      : 0u;

    static_assert(ActiveSim::count() > 0u, "the sim under parity must have entities");
}
