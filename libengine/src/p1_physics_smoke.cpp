/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P1 physics smoke — runs one CpuPhysicsBackend tick over ECS chunk storage,
** in-kernel. Three entities (Position/Velocity/Mass, no AABB so the pass is
** pure gravity integration) start at rest at y=100; after one 1/60 s step the
** semi-implicit Euler + velocity damping give a fixed result. Float math goes
** through SSE with -ffp-contract=off, so the IEEE bit patterns are identical to
** the Linux/xmake oracle — the P1 ECS+physics determinism gate.
*/
#include "libengine/libengine.h"

#include <lpl/ecs/Archetype.hpp>
#include <lpl/ecs/Component.hpp>
#include <lpl/ecs/Registry.hpp>
#include <lpl/math/Vec3.hpp>
#include <lpl/physics/CpuPhysicsBackend.hpp>

namespace {

inline lpl::core::u32 floatBits(float value)
{
    lpl::core::u32 bits = 0u;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

extern "C" void libengine_p1_physics_smoke(libengine_p1_physics_smoke_result_t *out)
{
    using namespace lpl::ecs;
    using lpl::core::u32;
    using Vec3f = lpl::math::Vec3<float>;

    if (out == nullptr)
        return;

    *out = libengine_p1_physics_smoke_result_t{};

    const ComponentId ids[] = {ComponentId::Position, ComponentId::Velocity, ComponentId::Mass};
    Archetype archetype{ids};

    Registry registry;

    constexpr u32 kCount = 3u;
    for (u32 i = 0u; i < kCount; ++i)
        (void) registry.createEntity(archetype);

    // Seed the chunk's component buffers: Position/Velocity live in the write
    // (back) buffer the integrator mutates; Mass in the read (front) buffer.
    auto partitions = registry.partitions();
    if (partitions.size() == 0u)
        return;

    u32 seeded = 0u;
    for (const auto &partition : partitions)
    {
        for (const auto &chunk : partition->chunks())
        {
            const u32 count = chunk->count();
            auto *positions = static_cast<Vec3f *>(chunk->writeComponent(ComponentId::Position));
            auto *velocities = static_cast<Vec3f *>(chunk->writeComponent(ComponentId::Velocity));
            auto *masses = static_cast<float *>(chunk->writeComponent(ComponentId::Mass));
            auto *massesRead = static_cast<float *>(const_cast<void *>(chunk->readComponent(ComponentId::Mass)));
            if (positions == nullptr || velocities == nullptr)
                continue;
            for (u32 i = 0u; i < count; ++i)
            {
                positions[i] = Vec3f{0.0f, 100.0f, 0.0f};
                velocities[i] = Vec3f{0.0f, 0.0f, 0.0f};
                if (masses != nullptr)
                    masses[i] = 1.0f;
                if (massesRead != nullptr)
                    massesRead[i] = 1.0f;
                ++seeded;
            }
        }
    }

    lpl::physics::CpuPhysicsBackend backend{registry};
    (void) backend.init();
    out->step_ok = backend.step(1.0f / 60.0f).has_value() ? 1u : 0u;

    // Read the integrated state back out of the write buffer.
    bool fell = true;
    u32 posYRaw = 0u;
    u32 velYRaw = 0u;
    u32 stepped = 0u;
    for (const auto &partition : partitions)
    {
        for (const auto &chunk : partition->chunks())
        {
            const u32 count = chunk->count();
            auto *positions = static_cast<Vec3f *>(chunk->writeComponent(ComponentId::Position));
            auto *velocities = static_cast<Vec3f *>(chunk->writeComponent(ComponentId::Velocity));
            if (positions == nullptr || velocities == nullptr)
                continue;
            for (u32 i = 0u; i < count; ++i)
            {
                if (stepped == 0u)
                {
                    posYRaw = floatBits(positions[i].y);
                    velYRaw = floatBits(velocities[i].y);
                }
                fell = fell && (positions[i].y < 100.0f) && (velocities[i].y < 0.0f);
                ++stepped;
            }
        }
    }

    out->entities_seeded = seeded;
    out->entities_stepped = stepped;
    out->position_y_raw = posYRaw;
    out->velocity_y_raw = velYRaw;
    out->fell_under_gravity_ok = fell ? 1u : 0u;
}
