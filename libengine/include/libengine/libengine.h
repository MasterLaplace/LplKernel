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

#ifdef __cplusplus
}
#endif

#endif /* _LIBENGINE_H */
