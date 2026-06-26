/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** libengine — C-callable facade for the freestanding LplPlugin engine module.
**
** libengine.a is the LplPlugin engine compiled -ffreestanding into the kernel
** image (the C++ sibling of libk.a). This header is the ONLY surface the pure-C
** kernel includes; it must stay free of C++ and of any lpl/ engine type.
*/
#ifndef _LIBENGINE_H
#define _LIBENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** P0 determinism smoke. Computes a handful of Fixed32 (Q16.16) and CORDIC
** results whose raw bit patterns are fixed by the math sources. The kernel
** prints these on serial; the xmake/host oracle computes the SAME values, so a
** byte-for-byte match proves the freestanding engine build is bit-identical to
** the Linux build (the HARD determinism contract, P0 exit gate).
*/
typedef struct {
    int32_t cordic_sin_quarter_pi_raw; /* sin(pi/4) in Q16.16 */
    int32_t cordic_cos_quarter_pi_raw; /* cos(pi/4) in Q16.16 */
    int32_t cordic_atan2_one_one_raw;  /* atan2(1, 1) in Q16.16 (~pi/4) */
    int32_t fixed_mul_three_half_raw;  /* (3.0 * 0.5) in Q16.16 = 1.5    */
    int32_t fixed_div_one_three_raw;   /* (1.0 / 3.0) in Q16.16          */
} libengine_p0_smoke_result_t;

extern void libengine_p0_smoke(libengine_p0_smoke_result_t *out);

/*
** P1 memory smoke. Exercises the engine's ArenaAllocator (lpl::memory) backed,
** on the kernel target, by the kernel heap (kmalloc) through the lpl/std/cstdlib
** umbrella. Proves the dependency-injected allocator seam works in-kernel: O(1)
** bump allocation with alignment, ownership query, O(1) reset, and graceful
** out-of-capacity failure. `used_after_allocations_bytes` is deterministic (the
** allocation sizes/alignments are fixed and the kmalloc slab is 8-byte aligned),
** so it matches the Linux/xmake oracle byte-for-byte.
*/
typedef struct {
    uint32_t allocations_aligned_ok;       /* every block honoured its requested alignment */
    uint32_t owns_pointer_ok;              /* ownsPtr true inside the arena, false outside  */
    uint32_t used_after_allocations_bytes; /* arena.used() after the fixed allocations       */
    uint32_t reset_reclaims_all_ok;        /* used() == 0 after reset()                      */
    uint32_t exhaustion_returns_null_ok;   /* an over-capacity allocate() returns nullptr    */
} libengine_p1_arena_smoke_result_t;

extern void libengine_p1_arena_smoke(libengine_p1_arena_smoke_result_t *out);

/*
** P1 ECS smoke. Exercises the engine's archetype/chunk SoA storage and the
** lock-free entity Registry (Treiber-stack slot free-list with generation
** counters) entirely in-kernel. Every field is fixed by the deterministic
** create/destroy sequence (10 created, slots 2/4/6 destroyed, slot 6 recycled
** first as generation 1), so it matches the Linux/xmake oracle byte-for-byte.
*/
typedef struct {
    uint32_t created_count;          /* entities successfully created (expect 10)       */
    uint32_t live_after_create;      /* registry liveCount after creation (expect 10)   */
    uint32_t first_entity_raw;       /* raw id of the first entity (gen 0, slot 0 => 0) */
    uint32_t destroyed_ok;           /* all three destroy() calls succeeded             */
    uint32_t live_after_destroy;     /* liveCount after destroying 3 (expect 7)         */
    uint32_t recycle_slot_lifo_ok;   /* first recycled slot is 6 (LIFO free-list)       */
    uint32_t recycle_generation_ok;  /* recycled entity carries generation 1            */
    uint32_t stale_id_dead_ok;       /* the old (gen 0, slot 6) id reports !isAlive      */
    uint32_t live_final;             /* liveCount after refilling (expect 10)           */
    uint32_t partition_entity_count; /* entities across the partition's chunks (expect 10) */
} libengine_p1_ecs_smoke_result_t;

extern void libengine_p1_ecs_smoke(libengine_p1_ecs_smoke_result_t *out);

/*
** P1 scheduler smoke. Drives the ECS SystemScheduler: four systems across two
** phases with a write/read hazard force a fixed DAG (A -> {B,D} -> C). The DAG
** is dispatched over the single-threaded InlineJobSystem, so the wave structure
** and execution order are deterministic and match the Linux/xmake oracle.
*/
typedef struct {
    uint32_t system_count;   /* registered systems (expect 4)                       */
    uint32_t build_ok;       /* buildGraph() found a valid topological order        */
    uint32_t exec_mask;      /* bit set per executed system, markers 1..4 => 0x1E   */
    uint32_t executed_count; /* systems that ran this tick (expect 4)               */
    uint32_t first_marker;   /* first system to run — phase Input => 1              */
    uint32_t last_marker;    /* last system to run — depends on B's output => 3     */
    uint32_t phase_cb_fired; /* post-Input phase callback fired exactly once        */
} libengine_p1_scheduler_smoke_result_t;

extern void libengine_p1_scheduler_smoke(libengine_p1_scheduler_smoke_result_t *out);

/*
** P1 physics smoke. Runs one CpuPhysicsBackend tick over real ECS chunk storage
** (three Position/Velocity/Mass entities at rest at y=100, no AABB so the pass
** is pure gravity integration). After one 1/60 s step the semi-implicit Euler +
** 0.995 velocity damping fix position.y/velocity.y exactly. The float math runs
** on SSE with -ffp-contract=off, so the raw IEEE bit patterns match the
** Linux/xmake oracle byte-for-byte — the P1 ECS+physics determinism gate.
*/
typedef struct {
    uint32_t entities_seeded;       /* entities written into chunk buffers (expect 3)   */
    uint32_t entities_stepped;      /* entities the physics tick processed (expect 3)   */
    uint32_t step_ok;               /* CpuPhysicsBackend::step returned success         */
    uint32_t position_y_raw;        /* IEEE-754 bits of position.y after the step       */
    uint32_t velocity_y_raw;        /* IEEE-754 bits of velocity.y after the step       */
    uint32_t fell_under_gravity_ok; /* every entity moved down with negative velocity   */
} libengine_p1_physics_smoke_result_t;

extern void libengine_p1_physics_smoke(libengine_p1_physics_smoke_result_t *out);

/*
** P2 HAL smoke. Drives the engine platform backends (lpl::platform) through
** their kernel HAL implementations: query the display surface + clear it and
** read a pixel back, read the clock tick/timestamp contract, drain the input
** ring, and allocate/translate/free pinned graphics memory. This proves the
** kernel platform seam is wired end to end (the P2 HAL bring-up gate). Unlike
** the P0/P1 smokes these values are NOT part of the bit-identical determinism
** contract: the surface geometry is host/QEMU-configuration dependent, and the
** tick/timestamp are wall-clock (non-deterministic by construction) — they are
** observability, not oracle-checked state.
*/
typedef struct {
    uint32_t display_available;    /* a framebuffer surface was reported          */
    uint32_t surface_width;        /* surface width in pixels                     */
    uint32_t surface_height;       /* surface height in pixels                    */
    uint32_t surface_bpp;          /* surface bits per pixel                      */
    uint32_t clear_readback_raw;   /* pixel(0,0) as 0x00RRGGBB after a clear       */
    uint32_t clear_readback_ok;    /* readback matched the written color           */
    uint32_t clock_tick_hertz;     /* clock backend reported frequency (Hz)        */
    uint32_t clock_tick_observed;  /* tickCount() snapshot (non-deterministic)     */
    uint32_t clock_tsc_advanced;   /* timestamp counter advanced and is non-zero   */
    uint32_t input_query_ok;       /* input ring drained without fault             */
    uint32_t input_pending_count;  /* decoded characters waiting (0 when headless) */
    uint32_t gpu_alloc_ok;         /* pinned graphics-memory allocation succeeded  */
    uint32_t gpu_physical_nonzero; /* physical address resolved for the allocation */
} libengine_p2_hal_smoke_result_t;

extern void libengine_p2_hal_smoke(libengine_p2_hal_smoke_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _LIBENGINE_H */
