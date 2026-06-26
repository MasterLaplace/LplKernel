/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P1 ECS smoke — exercises the engine Registry/Partition/Chunk (archetype SoA
** storage + lock-free slot free-list) entirely in-kernel. Every observable is
** fixed by the deterministic create/destroy sequence (slot allocation is a
** Treiber-stack LIFO, generation bumps on recycle), so the values match the
** Linux/xmake oracle bit-for-bit — the P1 determinism gate for the ECS module.
*/
#include "libengine/libengine.h"

#include <lpl/ecs/Archetype.hpp>
#include <lpl/ecs/Component.hpp>
#include <lpl/ecs/Entity.hpp>
#include <lpl/ecs/Registry.hpp>

extern "C" void libengine_p1_ecs_smoke(libengine_p1_ecs_smoke_result_t *out)
{
    using namespace lpl::ecs;
    using lpl::core::u32;

    if (out == nullptr)
        return;

    *out = libengine_p1_ecs_smoke_result_t{};

    // A fixed archetype: {Position, Velocity, Mass}. Order is irrelevant — the
    // archetype is a bitmask — so the resulting storage layout is deterministic.
    const ComponentId ids[] = {ComponentId::Position, ComponentId::Velocity, ComponentId::Mass};
    Archetype archetype{ids};

    Registry registry;

    // Create 10 entities → slots 0..9, all generation 0 (highWater bump path).
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
    out->first_entity_raw = created[0].raw(); // (gen 0, slot 0) → raw 0

    // Destroy slots 2, 4, 6 (in that order). Treiber-stack push order makes the
    // free-list head 6 → 4 → 2, so the next allocation pops slot 6 first (LIFO).
    bool destroyed = true;
    for (u32 idx : {2u, 4u, 6u})
        destroyed = destroyed && registry.destroyEntity(created[idx]).has_value();

    out->destroyed_ok = destroyed ? 1u : 0u;
    out->live_after_destroy = registry.liveCount(); // 10 - 3 = 7

    // The first recycled entity must take slot 6 with its generation bumped to 1.
    auto recycled = registry.createEntity(archetype);
    if (recycled.has_value())
    {
        out->recycle_slot_lifo_ok = (recycled.value().slot() == 6u) ? 1u : 0u;
        out->recycle_generation_ok = (recycled.value().generation() == 1u) ? 1u : 0u;
    }

    // The stale id (gen 0, slot 6) must now read as dead — generation mismatch.
    out->stale_id_dead_ok = registry.isAlive(created[6]) ? 0u : 1u;

    // Refill the remaining two recycled slots (4, then 2) to return to 10 live.
    (void) registry.createEntity(archetype);
    (void) registry.createEntity(archetype);
    out->live_final = registry.liveCount();

    // Total entities held across the single partition's chunks.
    u32 partitionEntities = 0u;
    for (const auto &partition : registry.partitions())
        partitionEntities += partition->entityCount();
    out->partition_entity_count = partitionEntities;
}
