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

#include <lpl/render/RenderParity.hpp>

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

    out->render_ok = (r0.in_front_count == 8u && rq.in_front_count == 8u &&
                      rq.screen_signature != r0.screen_signature && r0.vertex0_x > 0 && r0.vertex0_x < 1280 &&
                      r0.vertex0_y > 0 && r0.vertex0_y < 800)
                         ? 1u
                         : 0u;
}
