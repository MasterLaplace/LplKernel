/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** libengine — C-callable facade for the freestanding LplPlugin engine module.
**
** libengine.a is the LplPlugin engine compiled -ffreestanding into the kernel
** image (the C++ sibling of libk.a). This header is the ONLY surface the pure-C
** kernel includes; it must stay free of C++ and of any lpl/ engine type.
**
** Two kinds of entry point live here, and nothing else should be added lightly:
**   - libengine_client_app_run: the real entry. It constructs lpl::engine::Engine
**     with an injected platform and application, exactly as apps/client/main.cpp
**     does on Linux. The kernel passes no engine state and holds no game logic.
**   - the smoke/parity gates (P0..P6 + the simulation fold): C-callable only
**     because the kernel's smoke battery is C. They are diagnostics, not an API.
**
** There is deliberately no C simulation facade (init/step/render/entity_count):
** driving a sim from the kernel in C was scaffolding, and the IApplication seam
** replaced it.
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

/*
** P3 render smoke. Exercises the KernelDisplayRenderer (software rasterizer)
** over the kernel IDisplayBackend: N fixed-timestep frames, each advancing
** the Fixed32 rotation angle (deterministic CORDIC authority) and rasterising
** the triangle into the LFB via the HAL. Proves the P3 exit gate:
**   - FXSAVE/FXRSTOR in the ISR stub preserves FPU/SSE state across IRQs
**   - The software rasteriser writes triangle pixels to the surface
**   - The Fixed32 angle authority drives rotation deterministically
** The centre_pixel_raw and ticks_elapsed fields are observability only
** (wall-clock + QEMU-config dependent); smoke_ok is the gate.
*/
typedef struct {
    uint32_t display_available; /* framebuffer surface was available            */
    uint32_t renderer_init_ok;  /* KernelDisplayRenderer::init() succeeded      */
    uint32_t frames_rendered;   /* frames that completed beginFrame+endFrame     */
    uint32_t ticks_elapsed;     /* wall-clock ticks for the N frames (modular)  */
    uint32_t centre_pixel_raw;  /* pixel at (cx,cy) after last frame, 0xRRGGBB  */
    uint32_t triangle_visible;  /* centre pixel != background colour            */
    uint32_t smoke_ok;          /* overall gate: init + frames + visible        */
} libengine_p3_render_smoke_result_t;

extern void libengine_p3_render_smoke(libengine_p3_render_smoke_result_t *out);

/*
** P4 image smoke. Exercises the portable lpl::image module (integer-only color
** + Image container) inside the kernel and reports a determinism signature that
** MUST match the Linux oracle (tests/test-image-parity) bit-for-bit — image ops
** are non-authoritative but kept integer-exact to avoid cross-target drift.
*/
typedef struct {
    uint32_t red_hue;           /* rgbToHsb(red).hue       -> 0                    */
    uint32_t green_hue;         /* rgbToHsb(green).hue     -> 120                  */
    uint32_t blue_hue;          /* rgbToHsb(blue).hue      -> 240                  */
    uint32_t gray_roundtrip;    /* hsbToRgb(rgbToHsb(gray)) == gray (1/0)          */
    uint32_t white_luma;        /* luminanceOf(white)      -> 255                  */
    uint32_t hist_red_count;    /* histogram of 4x4 red: red[255] -> 16           */
    uint32_t centre_pixel;      /* bilinear centre of a 2x2 gradient, 0x00RRGGBB  */
    uint32_t painter_signature; /* FNV-1a fold of a fixed Painter scene         */
    uint32_t ppm_signature;     /* FNV-1a fold after a PPM write->read round-trip  */
    uint32_t smoke_ok;          /* all of the above match expected values          */
} libengine_p4_image_smoke_result_t;

extern void libengine_p4_image_smoke(libengine_p4_image_smoke_result_t *out);

/*
** P4 image present smoke. Paints a demo 2D scene (gradient + shapes via the
** lpl::image Painter) into a full-surface Image and blits it onto the display
** scanout through the IDisplayBackend HAL, then presents. Proves the
** Image -> hardware_abstraction_layer_display -> (virtio-gpu | software-LFB) path. Skipped when no
** surface is available (text-mode boot).
*/
typedef struct {
    uint32_t display_available; /* a presentable surface was found             */
    uint32_t width;             /* surface width                               */
    uint32_t height;            /* surface height                              */
    uint32_t image_signature;   /* FNV-1a fold of the painted scene             */
    uint32_t present_ok;        /* blit + present completed                     */
} libengine_p4_image_present_smoke_result_t;

extern void libengine_p4_image_present_smoke(libengine_p4_image_present_smoke_result_t *out);

/*
** P4 scene smoke. Exercises the lpl::scene graph (Fixed32 affine transforms,
** parent/child world composition, undo/redo, multi-select) in-kernel and
** reports raw Fixed32 values that must match the Linux oracle
** (tests/test-scene-parity) bit-for-bit (Fixed32/CORDIC authority).
*/
typedef struct {
    uint32_t world_tx_raw; /* child world translation x, Q16.16 raw -> 15<<16  */
    uint32_t world_ty_raw; /* child world translation y, Q16.16 raw -> 20<<16  */
    uint32_t undo_tx_raw;  /* child local tx after one undo, raw -> 5<<16      */
    uint32_t redo_tx_raw;  /* child local tx after redo, raw -> 7<<16          */
    uint32_t selection;    /* selection count after select(root,child)         */
    int32_t rot_x_raw;     /* fromTRS(90deg).apply(1,0).x raw -> ~0            */
    int32_t rot_y_raw;     /* fromTRS(90deg).apply(1,0).y raw -> ~65536        */
    uint32_t scene_ok;     /* all expected values matched                      */
} libengine_p4_scene_smoke_result_t;

extern void libengine_p4_scene_smoke(libengine_p4_scene_smoke_result_t *out);

/*
** P5 render smoke. Projects a Fixed32-authored unit cube (CORDIC model
** rotation) through a perspective camera and folds the resulting screen
** coordinates + depths. Geometry/rotation is authoritative Fixed32; the
** view/projection/divide is float (SSE, -ffp-contract=off). The folded
** signatures must match the Linux oracle (tests/test-render-parity)
** bit-for-bit.
*/
typedef struct {
    uint32_t angle0_screen_sig;  /* FNV-1a fold of all 8 floored screen (x,y) at angle 0 */
    uint32_t angle0_depth_sig;   /* FNV-1a fold of all 8 quantized NDC depths at angle 0  */
    int32_t angle0_vertex0_x;    /* floored screen X of cube vertex 0 (witness)          */
    int32_t angle0_vertex0_y;    /* floored screen Y of cube vertex 0 (witness)          */
    uint32_t angle0_in_front;    /* vertices with w > 0 at angle 0 -> 8                   */
    uint32_t quarter_screen_sig; /* screen fold at pi/4 (must differ from angle0)         */
    uint32_t cull_total;         /* instance grid size -> 49 (7x7)                       */
    uint32_t cull_visible;       /* instances surviving the frustum cull -> 40           */
    uint32_t cull_visible_sig;   /* FNV-1a fold of the visible-index list                */
    uint32_t tex_sample_sig;     /* FNV-1a fold of 64 bilinear texture samples           */
    uint32_t lambert_rgb;        /* Lambert shade of a reference fragment, 0x00RRGGBB    */
    uint32_t blinn_rgb;          /* Blinn-Phong shade of the same fragment               */
    uint32_t render_ok;          /* all expected invariants held                         */
} libengine_p5_render_smoke_result_t;

extern void libengine_p5_render_smoke(libengine_p5_render_smoke_result_t *out);

/*
** P5 render present. Rasterizes the depth-buffered cube into an offscreen
** buffer and presents a scaled copy onto the display scanout through the HAL
** (IDisplayBackend). The 96x64 offscreen fold (cube_signature) must match the
** Linux oracle (tests/test-render-parity cube angle0 sig) bit-for-bit.
*/
typedef struct {
    uint32_t display_available; /* surface present?                                   */
    uint32_t width;             /* surface width                                      */
    uint32_t height;            /* surface height                                     */
    uint32_t cube_signature;    /* FNV-1a fold of the 96x64 offscreen flat cube       */
    uint32_t textured_cube_sig; /* FNV-1a fold of the 96x64 offscreen textured cube   */
    uint32_t lit_cube_sig;      /* FNV-1a fold of the 96x64 Blinn-Phong lit cube      */
    uint32_t multiviewport_sig; /* FNV-1a fold of the 2x2 multi-viewport composite    */
    uint32_t rtt_sig;           /* FNV-1a fold of the render-to-texture cube          */
    uint32_t present_ok;        /* rasterized + presented without error               */
} libengine_p5_render_present_result_t;

extern void libengine_p5_render_present_smoke(libengine_p5_render_present_result_t *out);

/*
** P6 smoke. Advanced rendering on the kernel target: curve/surface topology
** (Catmull-Rom, parametric saddle, Delaunay), software ray tracing, metallic/
** roughness PBR with HDRI tone mapping, immutable command buffers with
** Late-Latching of Fixed32 poses, and foveated rasterization. Authoritative
** state is Fixed32; render math is float (SSE, -ffp-contract=off). Every
** signature below must match the Linux oracle (tests/test-p6-parity) bit-for-bit.
*/
typedef struct {
    uint32_t catmull_sig;        /* fold of the Catmull-Rom loop samples              */
    uint32_t saddle_sig;         /* fold of the parametric saddle surface vertices    */
    uint32_t delaunay_tris;      /* Delaunay triangle count -> 6                      */
    uint32_t delaunay_sig;       /* fold of the Delaunay triangle index list          */
    uint32_t ray_hits;           /* primary rays that hit geometry                    */
    uint32_t ray_image_sig;      /* fold of the ray-traced image                      */
    uint32_t pbr_gold_reinhard;  /* gold metal, Reinhard tone map, 0x00RRGGBB         */
    uint32_t pbr_gold_aces;      /* gold metal, ACES tone map                         */
    uint32_t pbr_plastic_aces;   /* blue dielectric, ACES tone map                    */
    uint32_t cmd_recording_sig;  /* fold of the immutable command-buffer recording    */
    uint32_t cmd_latched0_sig;   /* late-latched submit fold, pose set 0              */
    uint32_t cmd_latched1_sig;   /* late-latched submit fold, mutated poses           */
    uint32_t foveated_shaded;    /* foveated representative fragments shaded           */
    uint32_t foveated_full;      /* full-rate fragment count (width*height)           */
    uint32_t foveated_image_sig; /* fold of the replicated foveated image             */
    uint32_t p6_ok;              /* all expected invariants held                      */
} libengine_p6_smoke_result_t;

extern void libengine_p6_smoke(libengine_p6_smoke_result_t *out);

/*
** Simulation parity fold. Runs the active sample simulation (currently the
** CubePile crowd: N entities with Fixed32 position/velocity advanced by a
** deterministic gravity + floor-bounce + AABB-collision tick, each rasterized as
** a depth-buffered cube). Authoritative state is Fixed32 (bit-identical host vs
** kernel); the float render's folded image is bit-identical too. Both signatures
** below must match the Linux oracle (tests/parity) bit-for-bit.
*/
typedef struct {
    uint32_t state_sig_8;  /* authoritative Fixed32 state fold after 8 ticks    */
    uint32_t image_sig_8;  /* rendered image fold after 8 ticks                 */
    uint32_t state_sig_64; /* authoritative state fold after 64 ticks           */
    uint32_t image_sig_64; /* rendered image fold after 64 ticks                */
    uint32_t sim_ok;       /* invariants held (state evolved, image non-trivial) */
} libengine_sim_fold_result_t;

extern void libengine_sim_fold(libengine_sim_fold_result_t *out);

/*
** Procedural world parity fold. Bakes lpl::procgen::parityWorldRecipe() — fBm
** terrain, thermal and hydraulic erosion, depression filling and drainage, river
** carving, the rainfall/rain-shadow climate, Whittaker biomes, a blue-noise prop
** scatter, a cellular cave with its connectivity repair, a Voronoi-districted
** settlement, then the playability verdict — into a real ECS registry and folds
** what it produced.
**
** Three signatures rather than one. The entity fold only sees where cubes ended
** up, so every pass that reshapes the terrain without moving one — the climate,
** most of erosion — would be invisible to it. Folding the height field and the
** biome map puts the grids under the contract, which is where the arithmetic
** that could diverge between targets actually lives.
**
** A generated world is authoritative state, not authoring tooling, so it falls
** under the same HARD determinism contract as the simulation fold above: the
** Linux oracle (tests/parity/test_world_recipe.cpp) bakes the SAME recipe from
** the SAME constexpr definition and must produce identical signatures. The
** recipe lives in one place (lpl/procgen/WorldRecipe.hpp) precisely so the two
** sides cannot drift by editing separate copies of the parameters.
**
** The recipe is decoded from a baked .lplpak image through lpl::pack, the same
** freestanding reader a cartridge or a network transfer will feed. The editor
** authors a .lplscene document, a host tool bakes it, and the kernel rebuilds
** the world from the bytes — no JSON parser in ring 0.
*/
typedef struct {
    uint32_t pack_ok;          /* the baked .lplpak image opened and decoded        */
    uint32_t from_cartridge;   /* 1 = bytes came from a boot module, 0 = built-in   */
    uint32_t entity_count;     /* entities materialised by every pass               */
    uint32_t state_sig;        /* FNV-1a fold of authoritative Fixed32 entity state */
    uint32_t height_sig;       /* FNV-1a fold of the final height field             */
    uint32_t biome_sig;        /* FNV-1a fold of the biome map                      */
    uint32_t river_cells;      /* cells carved as river                             */
    uint32_t road_cells;       /* cells the road network occupies                   */
    uint32_t lake_cells;       /* cells holding standing water                      */
    uint32_t cave_floor;       /* open cells in the underground layer               */
    uint32_t plots;            /* building footprints the settlement laid out       */
    uint32_t gate_reachable;   /* 1 if the playability gate found the goal reachable */
    uint32_t gate_visited;     /* cells the gate's flood actually reached           */
    uint32_t gate_path_length; /* steps from entrance to exit                       */
    uint32_t world_ok;         /* world is non-empty AND passes its gate            */
} libengine_procgen_fold_result_t;

/*
** Bakes a world from a .lplpak image. Pass the bytes of a boot module (the
** cartridge) to load a real game; pass NULL to fall back to the reference pack
** compiled into the image, which is what the parity gate folds when no
** cartridge is present. Either way the SAME freestanding reader runs.
*/
extern void libengine_procgen_fold_from(const void *pack_bytes, uint32_t pack_size,
                                        libengine_procgen_fold_result_t *out);

/* Convenience: libengine_procgen_fold_from(NULL, 0, out). */
extern void libengine_procgen_fold(libengine_procgen_fold_result_t *out);

/*
** Living simulation parity fold. Runs lpl::ecology::parityLivingRecipe() — a
** four-level trophic web, a breeding population under mutation, a pheromone
** field with agents walking it, a flock, an abstract world migrating creatures
** between rooms under a realisation budget, and the pack life cycle — for a
** fixed number of ticks, then folds every subsystem.
**
** This is the fold the world gate CANNOT make. libengine_procgen_fold above
** folds a world that was generated: it proves the shape of the world crosses
** targets intact, and then stops, because a recipe's last pass is the last thing
** it can see. Everything ai/ and ecology/ do happens afterwards, so those two
** modules were linked into ring 0 with their determinism checked on the host and
** merely assumed on the target — the one assumption this project refuses to make
** anywhere else. This closes that.
**
** Four signatures, for the same reason the world gate has three: populations,
** genomes, the field and the social layer diverge for entirely unrelated
** reasons, and a single number would say only that something moved.
**
** The Linux oracle is tests/parity/test_living_parity.cpp, running the SAME
** recipe from the SAME constexpr definition in lpl/ecology/LivingRecipe.hpp.
*/
typedef struct {
    uint32_t population_sig; /* FNV-1a fold of every species' head count          */
    uint32_t genome_sig;     /* FNV-1a fold of every gene of every genome         */
    uint32_t stigmergy_sig;  /* FNV-1a fold of every channel of the field         */
    uint32_t social_sig;     /* FNV-1a fold of the abstract world and the packs   */
    uint32_t extinctions;    /* species that fell to their refuge floor           */
    uint32_t anomalies;      /* genomes k sigma above the population mean         */
    uint32_t realised_rooms; /* rooms holding bodies when the run ended           */
    uint32_t migrations;     /* abstract room transitions over the whole run      */
    uint32_t alpha_changes;  /* times a pack changed leader                       */
    uint32_t trail_cells;    /* field cells still above the evaporation floor     */
    uint32_t living_ok;      /* the run is well formed (see LivingRecipe.hpp)     */
} libengine_living_fold_result_t;

extern void libengine_living_fold(libengine_living_fold_result_t *out);

/*
** Endless-world parity fold. Generates a patch of the chunked, edgeless world —
** terrain sampled at absolute coordinates, rivers decided by a bounded basin
** plus a coarse trunk level — and folds it.
**
** P7 folds the world a recipe BUILDS; P8 folds the simulation that RUNS on it.
** Neither says anything about the world that streams, which had its determinism
** checked on the host and assumed on the target. This closes the last of the
** three.
**
** The seam count travels with the signatures deliberately. A fold proves two
** machines agree; it does not prove they agree on something correct, and a
** chunked world that seams identically on both targets would pass a signature
** check every single time.
**
** The Linux oracle is tests/parity/test_procgen_chunking.cpp.
*/
typedef struct {
    uint32_t height_sig;      /* FNV-1a over every cell of every chunk folded    */
    uint32_t river_sig;       /* FNV-1a over the river masks                     */
    uint32_t chunks;          /* chunks visited                                  */
    uint32_t river_cells;     /* cells carrying water                            */
    uint32_t seam_mismatches; /* height disagreements across the patch's seams   */
} libengine_endless_fold_result_t;

extern void libengine_endless_fold(libengine_endless_fold_result_t *out);

/**
 * @brief Folds of the shapes the L-system grammars grow (P10).
 *
 * A tree is scenery, but every branch endpoint comes out of a CORDIC rotation,
 * which is the arithmetic the whole determinism contract rests on.
 */
typedef struct {
    uint32_t conifer_sig;      /**< FNV-1a over the conifer skeleton. */
    uint32_t broadleaf_sig;    /**< FNV-1a over the broadleaf skeleton. */
    uint32_t shrub_sig;        /**< FNV-1a over the shrub skeleton. */
    uint32_t conifer_segments; /**< Wood segments the conifer grew. */
    uint32_t conifer_leaves;   /**< Foliage clusters the conifer grew. */
} libengine_botany_fold_result_t;

extern void libengine_botany_fold(libengine_botany_fold_result_t *out);

/*
** Kernel client entry point — the freestanding mirror of apps/client/main.cpp.
**
** Takes the cartridge the same way the parity fold does: pass the bytes of a
** .lplpak boot module to run THAT game, or NULL to fall back to the reference
** pack compiled into the image. The world the viewer draws is then the world the
** .lplscene document describes, decoded by the same freestanding reader — not a
** pipeline the cartridge cannot reach.
** Builds an engine Config, constructs lpl::engine::Engine with a KernelPlatform
** and an application payload, then init/run/shutdown. Blocks until the payload
** requests shutdown. The kernel passes no game state: which simulation runs is
** decided entirely engine-side, by the payload libengine/src/client_app.cpp
** injects.
*/
extern void libengine_client_app_run(const void *pack_bytes, uint32_t pack_size);

/*
** Kernel server entry point — the freestanding mirror of apps/server/main.cpp.
** Builds an engine Config, constructs lpl::engine::Engine with a KernelPlatform
** and an application payload, then init/run/shutdown. Blocks until the payload
** requests shutdown. The kernel passes no game state: which simulation runs is
** decided entirely engine-side, by the payload libengine/src/server_app.cpp injects.
*/
extern void libengine_server_app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* _LIBENGINE_H */
