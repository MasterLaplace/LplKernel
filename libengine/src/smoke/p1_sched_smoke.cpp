#include "libengine/libengine.h"

#include <lpl/concurrency/IJobSystem.hpp>
#include <lpl/ecs/Component.hpp>
#include <lpl/ecs/System.hpp>
#include <lpl/ecs/SystemScheduler.hpp>
#include <lpl/std/memory.hpp>

#include <span>

namespace {

using namespace lpl::ecs;
using lpl::core::f32;
using lpl::core::u32;

/**
 * Shared scratch the systems write to as they run; every field is determined by the fixed
 * execution order, never by wall-clock or threading.
 */
struct SmokeContext {
    u32 mask = 0u;       /**< Bit `marker` set when that system executes. */
    u32 order[8] = {};   /**< Markers in execution order. */
    u32 orderCount = 0u; /**< Number of systems that have executed. */
    u32 cbFired = 0u;    /**< Set by the post-Input phase callback. */
};

/** A trivial system that records its marker. `phase` and `accesses` drive the DAG. */
class MarkerSystem final : public ISystem {
public:
    MarkerSystem(SmokeContext *ctx, u32 marker, SchedulePhase phase, std::span<const ComponentAccess> accesses)
        : _ctx{ctx}, _marker{marker}, _descriptor{"", phase, accesses}
    {
    }

    const SystemDescriptor &descriptor() const noexcept override { return _descriptor; }

    void execute([[maybe_unused]] f32 dt) override
    {
        _ctx->mask |= (1u << _marker);
        _ctx->order[_ctx->orderCount++] = _marker;
    }

private:
    SmokeContext *_ctx;
    u32 _marker;
    SystemDescriptor _descriptor;
};

/** Static access tables: the descriptor stores a span over them, so they must outlive it. */
constexpr ComponentAccess kAccessA[] = {{ComponentId::Position, AccessMode::ReadWrite}};
constexpr ComponentAccess kAccessB[] = {{ComponentId::Position, AccessMode::ReadOnly},
                                        {ComponentId::Velocity, AccessMode::ReadWrite}};
constexpr ComponentAccess kAccessC[] = {{ComponentId::Velocity, AccessMode::ReadOnly}};
constexpr ComponentAccess kAccessD[] = {{ComponentId::Mass, AccessMode::ReadWrite}};

} // namespace

extern "C" void libengine_p1_scheduler_smoke(libengine_p1_scheduler_smoke_result_t *out)
{
    if (out == nullptr)
        return;

    *out = libengine_p1_scheduler_smoke_result_t{};

    SmokeContext ctx;

    lpl::concurrency::InlineJobSystem jobSystem;
    SystemScheduler scheduler{jobSystem};

    (void) scheduler.registerSystem(lpl::pmr::make_unique<MarkerSystem>(&ctx, 1u, SchedulePhase::Input, kAccessA));
    (void) scheduler.registerSystem(lpl::pmr::make_unique<MarkerSystem>(&ctx, 2u, SchedulePhase::Physics, kAccessB));
    (void) scheduler.registerSystem(lpl::pmr::make_unique<MarkerSystem>(&ctx, 3u, SchedulePhase::Physics, kAccessC));
    (void) scheduler.registerSystem(lpl::pmr::make_unique<MarkerSystem>(&ctx, 4u, SchedulePhase::Physics, kAccessD));

    out->system_count = scheduler.systemCount();

    scheduler.setPhaseCallback(SchedulePhase::Input, [&ctx]() { ++ctx.cbFired; });

    out->build_ok = scheduler.buildGraph().has_value() ? 1u : 0u;

    scheduler.tick(1.0f);

    out->exec_mask = ctx.mask;
    out->first_marker = (ctx.orderCount > 0u) ? ctx.order[0] : 0u;
    out->last_marker = (ctx.orderCount > 0u) ? ctx.order[ctx.orderCount - 1u] : 0u;
    out->executed_count = ctx.orderCount;
    out->phase_cb_fired = ctx.cbFired;
}
