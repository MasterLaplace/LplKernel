#include "libengine/libengine.h"

#include <lpl/ecs/Archetype.hpp>
#include <lpl/ecs/Component.hpp>
#include <lpl/ecs/Registry.hpp>
#include <lpl/math/Vec3.hpp>
#include <lpl/physics/CpuPhysicsBackend.hpp>

namespace {

using PartitionSpan = std::span<const lpl::pmr::unique_ptr<lpl::ecs::Partition>>;
using Vec3f = lpl::math::Vec3<float>;

inline lpl::core::u32 floatBits(float value)
{
    lpl::core::u32 bits = 0u;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/**
 * @brief Seeds every entity at rest at y = 100 with a unit mass.
 *
 * @details Position and velocity go into the write (back) buffer the integrator mutates.
 *          Mass is written to both buffers, because the integrator reads it from the read
 *          (front) one.
 *
 * @param partitions The registry's partitions.
 * @return Entities seeded.
 */
lpl::core::u32 seedAtRest(PartitionSpan partitions)
{
    lpl::core::u32 seeded = 0u;
    for (const auto &partition : partitions)
    {
        for (const auto &chunk : partition->chunks())
        {
            const lpl::core::u32 count = chunk->count();
            auto *positions = static_cast<Vec3f *>(chunk->writeComponent(lpl::ecs::ComponentId::Position));
            auto *velocities = static_cast<Vec3f *>(chunk->writeComponent(lpl::ecs::ComponentId::Velocity));
            auto *masses = static_cast<float *>(chunk->writeComponent(lpl::ecs::ComponentId::Mass));
            auto *massesRead =
                static_cast<float *>(const_cast<void *>(chunk->readComponent(lpl::ecs::ComponentId::Mass)));
            if (positions == nullptr || velocities == nullptr)
                continue;
            for (lpl::core::u32 i = 0u; i < count; ++i)
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
    return seeded;
}

/**
 * @brief Reads the integrated state back out of the write buffer into the result.
 *
 * @details The first entity's position.y and velocity.y are kept as raw IEEE bits; every
 *          entity must have fallen, with a negative velocity.
 *
 * @param partitions The registry's partitions.
 * @param out        Receives the stepped count, the raw bits and whether all fell.
 */
void readIntegratedState(PartitionSpan partitions, libengine_p1_physics_smoke_result_t *out)
{
    bool fell = true;
    lpl::core::u32 stepped = 0u;
    for (const auto &partition : partitions)
    {
        for (const auto &chunk : partition->chunks())
        {
            const lpl::core::u32 count = chunk->count();
            auto *positions = static_cast<Vec3f *>(chunk->writeComponent(lpl::ecs::ComponentId::Position));
            auto *velocities = static_cast<Vec3f *>(chunk->writeComponent(lpl::ecs::ComponentId::Velocity));
            if (positions == nullptr || velocities == nullptr)
                continue;
            for (lpl::core::u32 i = 0u; i < count; ++i)
            {
                if (stepped == 0u)
                {
                    out->position_y_raw = floatBits(positions[i].y);
                    out->velocity_y_raw = floatBits(velocities[i].y);
                }
                fell = fell && (positions[i].y < 100.0f) && (velocities[i].y < 0.0f);
                ++stepped;
            }
        }
    }

    out->entities_stepped = stepped;
    out->fell_under_gravity_ok = fell ? 1u : 0u;
}

} // namespace

extern "C" void libengine_p1_physics_smoke(libengine_p1_physics_smoke_result_t *out)
{
    using namespace lpl::ecs;
    using lpl::core::u32;

    if (out == nullptr)
        return;

    *out = libengine_p1_physics_smoke_result_t{};

    const ComponentId ids[] = {ComponentId::Position, ComponentId::Velocity, ComponentId::Mass};
    Archetype archetype{ids};

    Registry registry;

    constexpr u32 kCount = 3u;
    for (u32 i = 0u; i < kCount; ++i)
        (void) registry.createEntity(archetype);

    auto partitions = registry.partitions();
    if (partitions.size() == 0u)
        return;

    const u32 seeded = seedAtRest(partitions);

    lpl::physics::CpuPhysicsBackend backend{registry};
    (void) backend.init();
    out->step_ok = backend.step(1.0f / 60.0f).has_value() ? 1u : 0u;

    readIntegratedState(partitions, out);
    out->entities_seeded = seeded;
}
