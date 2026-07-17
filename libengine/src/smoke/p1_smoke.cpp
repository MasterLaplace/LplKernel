/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P1 memory smoke — exercises the engine ArenaAllocator over the injected
** allocator seam. On the kernel target the arena slab is served by kmalloc
** (via the lpl/std/cstdlib umbrella); the bump offsets it reports are fixed by
** the allocation sizes/alignments, so they match the Linux/xmake oracle.
*/
#include "libengine/libengine.h"

#include <lpl/memory/ArenaAllocator.hpp>

extern "C" void libengine_p1_arena_smoke(libengine_p1_arena_smoke_result_t *out)
{
    using lpl::memory::ArenaAllocator;

    if (out == nullptr)
        return;

    constexpr lpl::core::usize CAPACITY = 256u;
    ArenaAllocator arena(CAPACITY);

    // Fixed sizes/alignments (all <= the 8-byte kmalloc slab alignment) so the
    // resulting bump offsets are deterministic regardless of the slab address.
    void *const a = arena.allocate(64u, 8u);
    void *const b = arena.allocate(32u, 8u);
    void *const c = arena.allocate(16u, 8u);

    const bool allocations_succeeded = (a != nullptr) && (b != nullptr) && (c != nullptr);
    const bool aligned =
        allocations_succeeded && ((reinterpret_cast<lpl::core::usize>(a) & 7u) == 0u) &&
        ((reinterpret_cast<lpl::core::usize>(b) & 7u) == 0u) && ((reinterpret_cast<lpl::core::usize>(c) & 7u) == 0u);

    int stack_marker = 0;
    const bool owns =
        allocations_succeeded && arena.ownsPtr(a) && arena.ownsPtr(c) && !arena.ownsPtr(&stack_marker);

    const lpl::core::usize used = arena.used();

    // Over-capacity request on the (nearly full) arena must fail gracefully.
    const bool exhaustion_null = (arena.allocate(CAPACITY, 8u) == nullptr);

    arena.reset();
    const bool reset_ok = (arena.used() == 0u);

    out->allocations_aligned_ok = aligned ? 1u : 0u;
    out->owns_pointer_ok = owns ? 1u : 0u;
    out->used_after_allocations_bytes = static_cast<uint32_t>(used);
    out->reset_reclaims_all_ok = reset_ok ? 1u : 0u;
    out->exhaustion_returns_null_ok = exhaustion_null ? 1u : 0u;
}
