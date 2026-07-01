/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Simulation runtime — the C-callable facade the generic application runtime
** drives to run the active sample simulation (currently lpl::samples::CubePile)
** on any host. Exposes a deterministic parity fold plus a stateful step/render
** pair (fixed-timestep simulation + uncapped render). The active sim is selected
** by the single type alias below — swap it to plug in a different game/sim; the
** kernel and the host frontends stay unchanged.
**
** Authoritative state is Fixed32 (bit-identical host vs kernel); render math is
** float (SSE, -ffp-contract=off). Every reported signature must equal the Linux
** oracle (tests/parity) bit-for-bit.
*/
#include "libengine/libengine.h"

#include <lpl/samples/CubePile.hpp>

namespace {
// The active simulation. Swapping this single alias plugs in a different sim.
using ActiveSim = lpl::samples::CubePile;
} // namespace

// ---------------------------------------------------------------------------
// Deterministic parity fold — runs the sim headless for a fixed tick count into
// an internal target and folds authoritative state + rendered image. Dimensions
// must match the oracle (tests/parity) for the image fold to agree.
// ---------------------------------------------------------------------------
namespace {
constexpr lpl::core::u32 kW = 192u;
constexpr lpl::core::u32 kH = 120u;
lpl::core::u32 g_color[kW * kH];
lpl::core::f32 g_depth[kW * kH];
} // namespace

extern "C" void libengine_sim_fold(libengine_sim_fold_result_t *out)
{
    using namespace lpl;

    if (out == nullptr)
        return;
    *out = libengine_sim_fold_result_t{};

    render::RenderTarget rt{g_color, g_depth, kW, kH};

    const auto early = samples::runCubePileAndFold(rt, 8u);
    const auto late = samples::runCubePileAndFold(rt, 64u);

    out->state_sig_8 = early.state_signature;
    out->image_sig_8 = early.image_signature;
    out->state_sig_64 = late.state_signature;
    out->image_sig_64 = late.image_signature;
    out->sim_ok = (early.state_signature != late.state_signature &&
                   late.image_signature != render::detail::kFnv1aOffsetBasis)
                      ? 1u
                      : 0u;
}

// ---------------------------------------------------------------------------
// Stateful present facade — lets a host (kernel HAL or Linux) drive the live
// sim frame by frame. The sim persists across calls; each frame is rendered into
// a fixed internal target the host scales onto its display. Non-authoritative
// (live animation), not folded.
// ---------------------------------------------------------------------------
namespace {
constexpr lpl::core::u32 kPW = 480u;
constexpr lpl::core::u32 kPH = 300u;
lpl::core::u32 g_pcolor[kPW * kPH];
lpl::core::f32 g_pdepth[kPW * kPH];
ActiveSim g_sim;
} // namespace

extern "C" void libengine_sim_init(void) { g_sim.init(); }

extern "C" uint32_t libengine_sim_entity_count(void) { return lpl::samples::CubePile::count(); }

// Advance the simulation by one fixed step. Decoupled from rendering so the host
// can run the sim at a fixed rate (real-time correct) while rendering as fast as
// the display allows (uncapped) — the classic fixed-timestep / render split.
// Calling this exactly tick_hz times per second keeps animation speed
// independent of frame rate.
extern "C" void libengine_sim_step(void) { g_sim.step(); }

// Render the current state through the host-driven orbit camera and return the
// pixels. Pure render (no step) — safe to call many times per simulation tick.
// @p yaw/pitch/dist orbit the target; @p possess (>=0) follows that entity, else
// the camera orbits the pile centre.
extern "C" const lpl::core::u32 *libengine_sim_render(float yaw, float pitch, float dist, lpl::core::i32 possess,
                                                      lpl::core::u32 *out_w, lpl::core::u32 *out_h)
{
    using namespace lpl;
    render::RenderTarget rt{g_pcolor, g_pdepth, kPW, kPH};
    g_sim.render(rt, samples::CubePile::Camera{yaw, pitch, dist, possess});
    if (out_w != nullptr)
        *out_w = kPW;
    if (out_h != nullptr)
        *out_h = kPH;
    return g_pcolor;
}
