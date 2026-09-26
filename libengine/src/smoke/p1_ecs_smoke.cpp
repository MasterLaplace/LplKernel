#include "libengine/libengine.h"

#include <lpl/ecs/Archetype.hpp>
#include <lpl/ecs/Component.hpp>
#include <lpl/ecs/Entity.hpp>
#include <lpl/ecs/Registry.hpp>

namespace {

/**
 * @brief Entities held across every partition's chunks.
 * @param registry The registry to count.
 * @return The total.
 */
lpl::core::u32 countPartitionEntities(lpl::ecs::Registry &registry)
{
    lpl::core::u32 entities = 0u;
    for (const auto &partition : registry.partitions())
        entities += partition->entityCount();
    return entities;
}

} // namespace

extern "C" void libengine_p1_ecs_smoke(libengine_p1_ecs_smoke_result_t *out)
{
    using namespace lpl::ecs;
    using lpl::core::u32;

    if (out == nullptr)
        return;

    *out = libengine_p1_ecs_smoke_result_t{};

    const ComponentId ids[] = {ComponentId::Position, ComponentId::Velocity, ComponentId::Mass};
    Archetype archetype{ids};

    Registry registry;

    constexpr u32 kCount = 10u;
    EntityId created[kCount];
    u32 createdCount = 0u;
    for (u32 i = 0u; i < kCount; ++i)
    {
        auto result = registry.createEntity(archetype);
        if (!result.has_value())
            break;
        created[i] = result.value();
        ++createdCount;
    }

    out->created_count = createdCount;
    out->live_after_create = registry.liveCount();
    out->first_entity_raw = created[0].raw();

    bool destroyed = true;
    for (u32 idx : {2u, 4u, 6u})
        destroyed = destroyed && registry.destroyEntity(created[idx]).has_value();

    out->destroyed_ok = destroyed ? 1u : 0u;
    out->live_after_destroy = registry.liveCount();

    auto recycled = registry.createEntity(archetype);
    if (recycled.has_value())
    {
        out->recycle_slot_lifo_ok = (recycled.value().slot() == 6u) ? 1u : 0u;
        out->recycle_generation_ok = (recycled.value().generation() == 1u) ? 1u : 0u;
    }

    out->stale_id_dead_ok = registry.isAlive(created[6]) ? 0u : 1u;

    (void) registry.createEntity(archetype);
    (void) registry.createEntity(archetype);
    out->live_final = registry.liveCount();
    out->partition_entity_count = countPartitionEntities(registry);
}
