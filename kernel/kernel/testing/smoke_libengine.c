#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/testing/smoke_libengine.h>

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)

#    include <kernel/boot/boot_module.h>
#    include <kernel/boot/init_array.h>
#    include <kernel/diag/telemetry.h>
#    include <kernel/hal/hal.h>
#    include <kernel/hal/hal_audio.h>
#    include <kernel/cpu/pic.h>
#    include <kernel/drivers/hda.h>
#    include <kernel/power/frequency_scaling.h>
#    include <kernel/power/processor_sleep.h>
#    include <kernel/power/wakeup_accounting.h>

#    include <libengine/libengine.h>

#    if !defined(LPL_ASSISTANT_UNAVAILABLE)
#        include <kernel/ai/inference_budget.h>
#        include <kernel/ai/model_slot.h>
#        include <kernel/ai/tensor_arena.h>
#        include <kernel/dialogue/dialogue_channel.h>
#        include <kernel/drivers/hda.h>
#        include <kernel/satellite/satellite_app.h>
#        include <libassistant/libassistant.h>
#    endif

#    if !defined(LPL_KNOWLEDGE_UNAVAILABLE)
#        include <libknowledge/libknowledge.h>
#    endif

/** One labelled value of a smoke report line. */
typedef struct {
    const char *label;
    uint32_t value;
} SmokeLibengineRow_t;

/** Element count of a row table whose size is known at compile time. */
#    define SMOKE_LIBENGINE_ROW_COUNT(rows) (sizeof(rows) / sizeof((rows)[0]))

/**
 * @brief Writes @p prefix, then every row as its label followed by its value in hexadecimal.
 *
 * @details Leaves the line open, so a caller can append fields that are not numbers.
 *
 * @param com1   Serial port the line is written to.
 * @param prefix Text that opens the line.
 * @param rows   The rows.
 * @param count  How many rows there are.
 */
static void smoke_libengine_write_rows(Serial_t *com1, const char *prefix, const SmokeLibengineRow_t *rows,
                                       size_t count)
{
    serial_write_string(com1, prefix);
    for (size_t i = 0u; i < count; ++i)
    {
        serial_write_string(com1, rows[i].label);
        serial_write_hex32(com1, rows[i].value);
    }
}

/**
 * @brief Writes a whole report line: @p prefix, every row, then the end of the line.
 *
 * @param com1   Serial port the line is written to.
 * @param prefix Text that opens the line.
 * @param rows   The rows.
 * @param count  How many rows there are.
 */
static void smoke_libengine_write_line(Serial_t *com1, const char *prefix, const SmokeLibengineRow_t *rows,
                                       size_t count)
{
    smoke_libengine_write_rows(com1, prefix, rows, count);
    serial_write_string(com1, "\n");
}

/**
 * @brief The C++ constructor self-test: did static initialisation run before kernel_main?
 *
 * @details Proves the constructor machinery — `.ctors`/`.init_array` in linker.ld, `_init` and
 *          kernel_run_global_constructors — fired before kernel_main. The libengine C++ module
 *          is built on it.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_constructor_self_test(Serial_t *com1)
{
    if (kernel_constructor_self_test_passed())
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: C++ constructor self-test: PASS\n");
    else
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: C++ constructor self-test: FAIL\n");
}

/**
 * @brief Gate P0: Fixed32 and CORDIC results, as raw Q16.16 words.
 *
 * @details The freestanding engine computes results whose bit patterns are fixed by the math
 *          sources. Compared byte for byte against the Linux/xmake oracle, they prove
 *          bit-identical cross-target math — the HARD determinism contract.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p0_determinism(Serial_t *com1)
{
    libengine_p0_smoke_result_t smoke;
    libengine_p0_smoke(&smoke);
    const SmokeLibengineRow_t smoke_rows[] = {
        {"  sin(pi/4)   = ", (uint32_t) smoke.cordic_sin_quarter_pi_raw},
        {"  cos(pi/4)   = ", (uint32_t) smoke.cordic_cos_quarter_pi_raw},
        {"  atan2(1,1)  = ", (uint32_t) smoke.cordic_atan2_one_one_raw },
        {"  3.0 * 0.5   = ", (uint32_t) smoke.fixed_mul_three_half_raw },
        {"  1.0 / 3.0   = ", (uint32_t) smoke.fixed_div_one_three_raw  },
    };
    serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P0 determinism smoke (raw Q16.16):\n");
    for (size_t i = 0u; i < sizeof(smoke_rows) / sizeof(smoke_rows[0]); ++i)
    {
        serial_write_string(com1, smoke_rows[i].label);
        serial_write_hex32(com1, smoke_rows[i].value);
        serial_write_string(com1, "\n");
    }
}

/**
 * @brief Gate P1: the engine's ArenaAllocator, running in-kernel on a kmalloc'd slab.
 *
 * @details Proves the dependency-injected allocator seam: allocate, align, ownsPtr, reset and
 *          exhaustion. `used` is fixed by the allocation sizes, so it matches the Linux/xmake
 *          oracle.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p1_arena(Serial_t *com1)
{
    libengine_p1_arena_smoke_result_t arena;
    libengine_p1_arena_smoke(&arena);
    serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 arena smoke: aligned_ok=");
    serial_write_hex32(com1, arena.allocations_aligned_ok);
    serial_write_string(com1, ", owns_ok=");
    serial_write_hex32(com1, arena.owns_pointer_ok);
    serial_write_string(com1, ", used=");
    serial_write_hex32(com1, arena.used_after_allocations_bytes);
    serial_write_string(com1, ", reset_ok=");
    serial_write_hex32(com1, arena.reset_reclaims_all_ok);
    serial_write_string(com1, ", exhaustion_null_ok=");
    serial_write_hex32(com1, arena.exhaustion_returns_null_ok);
    serial_write_string(com1, "\n");
}

/**
 * @brief Gate P1: the archetype/chunk storage and the entity Registry, headless in-kernel.
 *
 * @details Each value is fixed by a deterministic create/destroy sequence, so it matches the
 *          Linux/xmake oracle.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p1_ecs(Serial_t *com1)
{
    libengine_p1_ecs_smoke_result_t ecs;
    libengine_p1_ecs_smoke(&ecs);
    const SmokeLibengineRow_t ecs_rows[] = {
        {"created=",              ecs.created_count         },
        {", live=",               ecs.live_after_create     },
        {", first_raw=",          ecs.first_entity_raw      },
        {", destroy_ok=",         ecs.destroyed_ok          },
        {", live_after_destroy=", ecs.live_after_destroy    },
        {", recycle_slot_ok=",    ecs.recycle_slot_lifo_ok  },
        {", recycle_gen_ok=",     ecs.recycle_generation_ok },
        {", stale_dead_ok=",      ecs.stale_id_dead_ok      },
        {", live_final=",         ecs.live_final            },
        {", part_count=",         ecs.partition_entity_count},
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 ECS smoke: ", ecs_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(ecs_rows));
}

/**
 * @brief Gate P1: the ECS DAG scheduler running four systems over the inline job system.
 *
 * @details The wave structure and the execution order are deterministic, so these values match
 *          the Linux/xmake oracle.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p1_scheduler(Serial_t *com1)
{
    libengine_p1_scheduler_smoke_result_t sched;
    libengine_p1_scheduler_smoke(&sched);
    const SmokeLibengineRow_t sched_rows[] = {
        {"systems=",     sched.system_count  },
        {", build_ok=",  sched.build_ok      },
        {", exec_mask=", sched.exec_mask     },
        {", executed=",  sched.executed_count},
        {", first=",     sched.first_marker  },
        {", last=",      sched.last_marker   },
        {", phase_cb=",  sched.phase_cb_fired},
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 scheduler smoke: ", sched_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(sched_rows));
}

/**
 * @brief Gate P1: one CpuPhysicsBackend gravity tick over ECS chunk storage.
 *
 * @details The float results run on SSE with `-ffp-contract=off`, so their raw IEEE bit patterns
 *          match the Linux/xmake oracle.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p1_physics(Serial_t *com1)
{
    libengine_p1_physics_smoke_result_t phys;
    libengine_p1_physics_smoke(&phys);
    const SmokeLibengineRow_t phys_rows[] = {
        {"seeded=",      phys.entities_seeded      },
        {", stepped=",   phys.entities_stepped     },
        {", step_ok=",   phys.step_ok              },
        {", pos_y_raw=", phys.position_y_raw       },
        {", vel_y_raw=", phys.velocity_y_raw       },
        {", fell_ok=",   phys.fell_under_gravity_ok},
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 physics smoke: ", phys_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(phys_rows));
}

/**
 * @brief Gate P2: the engine's platform backends, through their kernel HAL implementations.
 *
 * @details Display surface query, clear and readback, the clock tick and timestamp contract, the
 *          input ring drain, and a pinned graphics-memory allocate/translate/free: the kernel
 *          platform seam, wired end to end.
 *
 * @note Observability only, NOT part of the determinism contract: the surface geometry depends
 *       on the QEMU configuration and the clock is wall-clock.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p2_hal(Serial_t *com1)
{
    libengine_p2_hal_smoke_result_t hal;
    libengine_p2_hal_smoke(&hal);
    const SmokeLibengineRow_t hal_rows[] = {
        {"display_ok=",      hal.display_available   },
        {", width=",         hal.surface_width       },
        {", height=",        hal.surface_height      },
        {", bpp=",           hal.surface_bpp         },
        {", clear_raw=",     hal.clear_readback_raw  },
        {", clear_ok=",      hal.clear_readback_ok   },
        {", tick_hz=",       hal.clock_tick_hertz    },
        {", tick=",          hal.clock_tick_observed },
        {", tsc_ok=",        hal.clock_tsc_advanced  },
        {", input_ok=",      hal.input_query_ok      },
        {", input_pending=", hal.input_pending_count },
        {", gpu_alloc_ok=",  hal.gpu_alloc_ok        },
        {", gpu_phys_ok=",   hal.gpu_physical_nonzero},
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P2 HAL smoke: ", hal_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(hal_rows));
}

/**
 * @brief Gate P3: a rotating Fixed32-authored triangle, software-rasterised onto the display.
 *
 * @details Runs whenever a presentable surface exists — the software LFB or a virtio-gpu
 *          scanout — and is skipped only on a pure text-mode boot.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p3_render(Serial_t *com1)
{
    if (!hardware_abstraction_layer_display_available())
        return;

    libengine_p3_render_smoke_result_t p3;
    libengine_p3_render_smoke(&p3);
    const SmokeLibengineRow_t p3_rows[] = {
        {"display_ok=",  p3.display_available},
        {", init_ok=",   p3.renderer_init_ok },
        {", frames=",    p3.frames_rendered  },
        {", ticks=",     p3.ticks_elapsed    },
        {", centre_px=", p3.centre_pixel_raw },
        {", visible=",   p3.triangle_visible },
        {", smoke_ok=",  p3.smoke_ok         },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P3 render smoke: ", p3_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(p3_rows));
}

/**
 * @brief Gate P4: the portable lpl::image module, run in-kernel.
 *
 * @details Its signature must match the Linux oracle, tests/test-image-parity.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p4_image(Serial_t *com1)
{
    libengine_p4_image_smoke_result_t img;
    libengine_p4_image_smoke(&img);
    const SmokeLibengineRow_t img_rows[] = {
        {"red_hue=",      img.red_hue          },
        {", green_hue=",  img.green_hue        },
        {", blue_hue=",   img.blue_hue         },
        {", gray_rt=",    img.gray_roundtrip   },
        {", white_luma=", img.white_luma       },
        {", hist255=",    img.hist_red_count   },
        {", centre=",     img.centre_pixel     },
        {", paint_sig=",  img.painter_signature},
        {", ppm_sig=",    img.ppm_signature    },
        {", smoke_ok=",   img.smoke_ok         },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P4 image smoke: ", img_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(img_rows));
}

/**
 * @brief Gate P4: the deterministic 2D scene graph — Fixed32 transforms, undo/redo, selection.
 *
 * @details Raw values must match the Linux oracle.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p4_scene(Serial_t *com1)
{
    libengine_p4_scene_smoke_result_t scn;
    libengine_p4_scene_smoke(&scn);
    const SmokeLibengineRow_t scn_rows[] = {
        {"world_tx=",   scn.world_tx_raw        },
        {", world_ty=", scn.world_ty_raw        },
        {", undo_tx=",  scn.undo_tx_raw         },
        {", redo_tx=",  scn.redo_tx_raw         },
        {", sel=",      scn.selection           },
        {", rot_x=",    (uint32_t) scn.rot_x_raw},
        {", rot_y=",    (uint32_t) scn.rot_y_raw},
        {", scene_ok=", scn.scene_ok            },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P4 scene smoke: ", scn_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(scn_rows));
}

/**
 * @brief Gate P5: a Fixed32 unit cube, CORDIC-rotated through a perspective camera.
 *
 * @details The folded screen and depth signatures must match the Linux oracle,
 *          tests/test-render-parity, bit for bit.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p5_render(Serial_t *com1)
{
    libengine_p5_render_smoke_result_t rnd;
    libengine_p5_render_smoke(&rnd);
    const SmokeLibengineRow_t rnd_rows[] = {
        {"screen_sig=",    rnd.angle0_screen_sig          },
        {", depth_sig=",   rnd.angle0_depth_sig           },
        {", v0_x=",        (uint32_t) rnd.angle0_vertex0_x},
        {", v0_y=",        (uint32_t) rnd.angle0_vertex0_y},
        {", in_front=",    rnd.angle0_in_front            },
        {", quarter_sig=", rnd.quarter_screen_sig         },
        {", cull_vis=",    rnd.cull_visible               },
        {", cull_sig=",    rnd.cull_visible_sig           },
        {", tex_sig=",     rnd.tex_sample_sig             },
        {", lambert=",     rnd.lambert_rgb                },
        {", blinn=",       rnd.blinn_rgb                  },
        {", render_ok=",   rnd.render_ok                  },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P5 render smoke: ", rnd_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(rnd_rows));
}

/**
 * @brief Gate P6: advanced rendering.
 *
 * @details Topology, software ray tracing, metallic/roughness PBR with an HDRI tone map,
 *          immutable command buffers with late latching, and foveated rasterisation. Every
 *          signature must match the Linux oracle, tests/test-p6-parity, bit for bit.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p6_advanced_rendering(Serial_t *com1)
{
    libengine_p6_smoke_result_t p6;
    libengine_p6_smoke(&p6);
    const SmokeLibengineRow_t p6_rows[] = {
        {"catmull_sig=",    p6.catmull_sig       },
        {", saddle_sig=",   p6.saddle_sig        },
        {", del_tris=",     p6.delaunay_tris     },
        {", del_sig=",      p6.delaunay_sig      },
        {", ray_hits=",     p6.ray_hits          },
        {", ray_sig=",      p6.ray_image_sig     },
        {", gold_rein=",    p6.pbr_gold_reinhard },
        {", gold_aces=",    p6.pbr_gold_aces     },
        {", plastic_aces=", p6.pbr_plastic_aces  },
        {", cmd_rec=",      p6.cmd_recording_sig },
        {", latch0=",       p6.cmd_latched0_sig  },
        {", latch1=",       p6.cmd_latched1_sig  },
        {", fov_shaded=",   p6.foveated_shaded   },
        {", fov_sig=",      p6.foveated_image_sig},
        {", p6_ok=",        p6.p6_ok             },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P6 smoke: ", p6_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(p6_rows));
}

/**
 * @brief Gate P4: a painted 2D scene, blitted onto the scanout through the HAL.
 *
 * @note Runs after the other display gates so the 2D scene is what stays on screen.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p4_image_present(Serial_t *com1)
{
    if (!hardware_abstraction_layer_display_available())
        return;

    libengine_p4_image_present_smoke_result_t present;
    libengine_p4_image_present_smoke(&present);
    const SmokeLibengineRow_t present_rows[] = {
        {"display=",      present.display_available},
        {", width=",      present.width            },
        {", height=",     present.height           },
        {", img_sig=",    present.image_signature  },
        {", present_ok=", present.present_ok       },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P4 image present: ", present_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(present_rows));
}

/**
 * @brief Gate P5: the depth-buffered 3D cube, rasterised and presented scaled onto the scanout.
 *
 * @details The offscreen fold must match the oracle.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p5_render_present(Serial_t *com1)
{
    if (!hardware_abstraction_layer_display_available())
        return;

    libengine_p5_render_present_result_t r3d;
    libengine_p5_render_present_smoke(&r3d);
    const SmokeLibengineRow_t r3d_rows[] = {
        {"display=",        r3d.display_available},
        {", width=",        r3d.width            },
        {", height=",       r3d.height           },
        {", cube_sig=",     r3d.cube_signature   },
        {", tex_cube_sig=", r3d.textured_cube_sig},
        {", lit_cube_sig=", r3d.lit_cube_sig     },
        {", mv_sig=",       r3d.multiviewport_sig},
        {", rtt_sig=",      r3d.rtt_sig          },
        {", present_ok=",   r3d.present_ok       },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P5 render present: ", r3d_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(r3d_rows));
}

/**
 * @brief The sample simulation: a deterministic pile of cubes.
 *
 * @details N-entity gravity and bounce, AABB collision and cube rasterisation. The state and
 *          image folds must match the Linux oracle, tests/parity, bit for bit.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_cube_pile_simulation(Serial_t *com1)
{
    libengine_sim_fold_result_t sim;
    libengine_sim_fold(&sim);
    const SmokeLibengineRow_t sim_rows[] = {
        {"state8=",    sim.state_sig_8 },
        {", image8=",  sim.image_sig_8 },
        {", state64=", sim.state_sig_64},
        {", image64=", sim.image_sig_64},
        {", sim_ok=",  sim.sim_ok      },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine sim (cube pile): ", sim_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(sim_rows));
}

/**
 * @brief Folds the game cartridge GRUB loaded, or the built-in reference pack when there is none.
 *
 * @details A cartridge loaded alongside the kernel is the game to build. With no cartridge the
 *          reference pack keeps the parity gate meaningful.
 *
 * @note The module is looked up as "game.lplpak", not by the bare extension: the ISO also
 *       carries a cartridge for the world viewer, and matching on ".lplpak" would hand the gate
 *       whichever module GRUB listed first — folding a world the oracle never baked,
 *       intermittently, depending on module order.
 *
 * @param world Receives the fold.
 */
static void smoke_libengine_fold_game_world(libengine_procgen_fold_result_t *world)
{
    const uint8_t *cartridge = NULL;
    uint32_t cartridge_size = 0u;
    if (boot_module_find("game.lplpak", &cartridge, &cartridge_size))
        libengine_procgen_fold_from(cartridge, cartridge_size, world);
    else
        libengine_procgen_fold(world);
}

/**
 * @brief Gate P7: a seed baked into a real ECS world, grids AND authoritative state folded.
 *
 * @details Terrain, erosion, drainage and rivers, climate and biomes, blue-noise scatter, a
 *          cellular cave and a settlement. Must match tests/parity/test_world_recipe.cpp bit
 *          for bit; this is what lets the kernel replay a world by seed rather than by parsing
 *          a `.lplscene` in ring 0.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p7_procedural_world(Serial_t *com1)
{
    libengine_procgen_fold_result_t world;
    smoke_libengine_fold_game_world(&world);
    const SmokeLibengineRow_t world_rows[] = {
        {"pack_ok=",      world.pack_ok         },
        {", cartridge=",  world.from_cartridge  },
        {", entities=",   world.entity_count    },
        {", state_sig=",  world.state_sig       },
        {", height_sig=", world.height_sig      },
        {", biome_sig=",  world.biome_sig       },
        {", rivers=",     world.river_cells     },
        {", roads=",      world.road_cells      },
        {", lakes=",      world.lake_cells      },
        {", cave_floor=", world.cave_floor      },
        {", plots=",      world.plots           },
        {", reachable=",  world.gate_reachable  },
        {", visited=",    world.gate_visited    },
        {", path_len=",   world.gate_path_length},
        {", world_ok=",   world.world_ok        },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P7 procgen world: ", world_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(world_rows));
}

/**
 * @brief Gate P8: the living simulation, the fold P7 cannot make.
 *
 * @details P7 proves the world's SHAPE crosses targets intact and then stops, because a
 *          recipe's last pass is the last thing it can see — everything ai/ and ecology/ do
 *          happens afterwards. This runs a trophic web, a breeding population, a pheromone
 *          field with agents on it, a flock, an abstract world under a realisation budget and
 *          the pack life cycle for a fixed number of ticks, and folds all four. Must match
 *          tests/parity/test_living_parity.cpp bit for bit.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p8_living_simulation(Serial_t *com1)
{
    libengine_living_fold_result_t living;
    libengine_living_fold(&living);
    const SmokeLibengineRow_t living_rows[] = {
        {"population_sig=",  living.population_sig},
        {", genome_sig=",    living.genome_sig    },
        {", stigmergy_sig=", living.stigmergy_sig },
        {", social_sig=",    living.social_sig    },
        {", extinctions=",   living.extinctions   },
        {", anomalies=",     living.anomalies     },
        {", realised=",      living.realised_rooms},
        {", migrations=",    living.migrations    },
        {", alphas=",        living.alpha_changes },
        {", trail_cells=",   living.trail_cells   },
        {", living_ok=",     living.living_ok     },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P8 living sim: ", living_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(living_rows));
}

/**
 * @brief Gate P9: the world that STREAMS.
 *
 * @details P7 folds the world a recipe BUILDS, P8 the simulation that RUNS on it, and this one
 *          terrain at absolute coordinates, rivers from a bounded basin and a coarse trunk
 *          level. Must match tests/parity/test_procgen_chunking.cpp.
 *
 * @note The seam count is folded alongside the signatures because two targets agreeing on a
 *       seamed world would pass a signature check every time.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p9_endless_world(Serial_t *com1)
{
    libengine_endless_fold_result_t endless;
    libengine_endless_fold(&endless);
    const SmokeLibengineRow_t endless_rows[] = {
        {"height_sig=",  endless.height_sig     },
        {", river_sig=", endless.river_sig      },
        {", chunks=",    endless.chunks         },
        {", river=",     endless.river_cells    },
        {", seams=",     endless.seam_mismatches},
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P9 endless world: ", endless_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(endless_rows));
}

/**
 * @brief Gate P10: the shape a grammar grows.
 *
 * @details Folded because the 3D turtle is CORDIC end to end — the same rotations the camera
 *          basis and the terrain noise are built from. Must match
 *          tests/parity/test_botany_parity.cpp.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p10_botany(Serial_t *com1)
{
    libengine_botany_fold_result_t botany;
    libengine_botany_fold(&botany);
    const SmokeLibengineRow_t botany_rows[] = {
        {"conifer_sig=",     botany.conifer_sig     },
        {", broadleaf_sig=", botany.broadleaf_sig   },
        {", shrub_sig=",     botany.shrub_sig       },
        {", segments=",      botany.conifer_segments},
        {", leaves=",        botany.conifer_leaves  },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P10 botany: ", botany_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(botany_rows));
}

/**
 * @brief Gate P11: erasure coding, and the first gate whose two sides run different code.
 *
 * @details The host XORs 128 bits at a time, this build one word at a time, and the claim is
 *          that reordering associative, commutative, rounding-free operations changes nothing.
 *          Must match tests/parity/test_codec_parity.cpp.
 *
 * @note `vector` says which path THIS build took, so a run where both sides quietly took the
 *       scalar loop is visible rather than silently green.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p11_codec(Serial_t *com1)
{
    libengine_codec_fold_result_t codec;
    libengine_codec_fold(&codec);
    const SmokeLibengineRow_t codec_rows[] = {
        {"soliton_sig=",   codec.soliton_sig  },
        {", droplet_sig=", codec.droplet_sig  },
        {", matrix_sig=",  codec.matrix_sig   },
        {", payload_sig=", codec.payload_sig  },
        {", emitted=",     codec.emitted      },
        {", delivered=",   codec.delivered    },
        {", peeled=",      codec.peeled_blocks},
        {", recovered=",   codec.recovered    },
        {", vector=",      codec.vector_kernel},
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P11 codec: ", codec_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(codec_rows));
}

/**
 * @brief Gate P12: the artifact that carries its own reader.
 *
 * @details Folds the trace of a canonical program on the ten-opcode ISA, the engraved
 *          specification, the plate around it, and `selfhost` — whether a machine REBUILT from
 *          those engraved bytes runs the program to the same trace. That last one is the whole
 *          claim: a plate whose specification is not sufficient is a blob with decoration on
 *          it. Must match tests/parity/test_rosetta_isa.cpp.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p12_rosetta(Serial_t *com1)
{
    libengine_rosetta_fold_result_t rosetta;
    libengine_rosetta_fold(&rosetta);
    const SmokeLibengineRow_t rosetta_rows[] = {
        {"trace_sig=",     rosetta.trace_sig      },
        {", spec_sig=",    rosetta.spec_sig       },
        {", plate_sig=",   rosetta.plate_sig      },
        {", payload_sig=", rosetta.payload_sig    },
        {", steps=",       rosetta.steps          },
        {", halted=",      rosetta.halted         },
        {", opcodes=",     rosetta.rebuilt_opcodes},
        {", selfhost=",    rosetta.self_hosting   },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P12 rosetta: ", rosetta_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(rosetta_rows));
}

/**
 * @brief Gate P13: two sources, one past.
 *
 * @details A confidence in Fixed32 decides which of two contradictory claims becomes the
 *          consensus view, so it is authoritative state — a rounding that differed between
 *          targets would give two histories from one corpus. Must match
 *          tests/parity/test_history_parity.cpp.
 *
 * @note `minority` is the claim that matters most: the losing account must still be THERE, or
 *       how a myth was built can never be retraced.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p13_history(Serial_t *com1)
{
    libengine_history_fold_result_t history;
    libengine_history_fold(&history);
    const SmokeLibengineRow_t history_rows[] = {
        {"timeline_sig=",     history.timeline_sig      },
        {", chronicle_sig=",  history.chronicle_sig     },
        {", minority_sig=",   history.minority_sig      },
        {", constraints=",    history.constraints       },
        {", contradictions=", history.contradictions    },
        {", demoted=",        history.demoted           },
        {", minority=",       history.minority_reachable},
        {", scored=",         history.scored            },
        {", earned=",         history.earned            },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P13 history: ", history_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(history_rows));
}

/**
 * @brief Gate P20: a named body that walked, and what it earned.
 *
 * @details The first gate whose subject is somebody who MOVED — every earlier history
 *          signature folds a corpus being reasoned about, this one folds where a body ended up.
 *          Must match tests/parity/test_history_parity.cpp.
 *
 * @note `arrivals` is what makes the rest mean anything: a run in which nobody went anywhere
 *       folds perfectly stably on both targets and proves nothing, and `earned` against
 *       `scored` is the measurement itself.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p20_journey(Serial_t *com1)
{
    libengine_journey_fold_result_t journey;
    libengine_journey_fold(&journey);
    const SmokeLibengineRow_t journey_rows[] = {
        {"chronicle=",         journey.chronicle_sig     },
        {", position=",        journey.position_sig      },
        {", deed=",            journey.deed_sig          },
        {", seeded=",          journey.seeded            },
        {", forced=",          journey.forced            },
        {", unplaceable=",     journey.unplaceable       },
        {", arrivals=",        journey.arrivals          },
        {", scored=",          journey.scored            },
        {", earned=",          journey.earned            },
        {", score=",           journey.score             },
        {", first=",           journey.first_arrival     },
        {", routed=",          journey.routed            },
        {", avoided=",         journey.avoided           },
        {", road=",            journey.road_sig          },
        {", waypoints=",       journey.waypoint_sig      },
        {", roadcells=",       journey.road_cells        },
        {", roadpairs=",       journey.road_pairs        },
        {", altchron=",        journey.alt_chronicle     },
        {", altarr=",          journey.alt_arrivals      },
        {", closedchron=",     journey.closed_chronicle  },
        {", closedarr=",       journey.closed_arrivals   },
        {", closedfirst=",     journey.closed_first      },
        {", wrappedroad=",     journey.wrapped_road      },
        {", polarroad=",       journey.polar_road        },
        {", altfirst=",        journey.alt_first         },
        {", cascaderoad=",     journey.cascade_road_sig  },
        {", cascadecells=",    journey.cascade_road_cells},
        {", cascadecoarse=",   journey.cascade_coarse    },
        {", cascadecorridor=", journey.cascade_corridor  },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P20 journey: ", journey_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(journey_rows));
}

#    if !defined(LPL_ASSISTANT_UNAVAILABLE)

/**
 * @brief Boots the assistant and says whether the mind came up, with its slot and arena.
 *
 * @param com1 Serial port the report is written to.
 * @return true when the mind is ready.
 */
static bool smoke_libengine_report_assistant_boot(Serial_t *com1)
{
    const bool up = libassistant_boot(libassistant_recommended_arena_bytes());
    serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant boot: ");
    serial_write_string(com1, up ? "ready" : "FAILED");
    serial_write_string(com1, ", model slot=");
    serial_write_string(com1, libassistant_model_slot_state());
    serial_write_string(com1, ", arena=");
    serial_write_hex32(com1, (uint32_t) kernel_tensor_arena_size());
    serial_write_string(com1, "\n");
    return up;
}

/**
 * @brief Puts one question through the dialogue aperture and reports the round trip.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_dialogue_round_trip(Serial_t *com1)
{
    libassistant_dialogue_result_t dialogue;
    const bool answered = libassistant_dialogue_round_trip(&dialogue);
    const SmokeLibengineRow_t dialogue_rows[] = {
        {"question=",     dialogue.question_bytes},
        {", consumed=",   dialogue.consumed      },
        {", answer=",     dialogue.answer_bytes  },
        {", delivered=",  dialogue.delivered     },
        {", dropped=",    dialogue.dropped       },
        {", spent=",      dialogue.budget_spent  },
        {", denied=",     dialogue.budget_denied },
        {", answer_sig=", dialogue.answer_sig    },
        {", valid=",      dialogue.valid_call    },
    };
    smoke_libengine_write_rows(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant dialogue: ", dialogue_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(dialogue_rows));
    serial_write_string(com1, answered ? " (ok)\n" : " (no valid call)\n");
}

/**
 * @brief Gate P14: the demon thinks in ring 0, and thinks the same thing the host does.
 *
 * @details Everything is integer — eight-bit weights, Q16.16 activations, an exponential built
 *          from a shift and six Taylor terms, rotary angles from CORDIC — because a float
 *          forward pass would put the answer one rounding mode away from a different one, and
 *          every gate downstream of an answer assumes the answer is reproducible. Must match
 *          LplAssistant/tests/test_infer_parity.cpp.
 *
 * @note The aperture is exercised first and the gate second, in that order and not the other
 *       way round: the gate re-carves the tensor arena from zero, so the live mind does not
 *       survive it.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p14_mind(Serial_t *com1)
{
    if (smoke_libengine_report_assistant_boot(com1))
        smoke_libengine_report_dialogue_round_trip(com1);

    libassistant_mind_fold_result_t mind;
    libassistant_mind_fold(&mind);
    const SmokeLibengineRow_t mind_rows[] = {
        {"weight_sig=",        mind.weight_sig        },
        {", prompt_sig=",      mind.prompt_sig        },
        {", logit_sig=",       mind.logit_sig         },
        {", residual_sig=",    mind.residual_sig      },
        {", token_sig=",       mind.token_sig         },
        {", constrained_sig=", mind.constrained_sig   },
        {", text_sig=",        mind.text_sig          },
        {", vocab=",           mind.vocab             },
        {", prompt_tokens=",   mind.prompt_tokens     },
        {", generated=",       mind.generated         },
        {", draws=",           mind.draws             },
        {", constrained=",     mind.constrained_tokens},
        {", admitted=",        mind.admitted_first    },
        {", grammar_done=",    mind.grammar_complete  },
        {", forbidden=",       mind.forbidden         },
        {", blob_bytes=",      mind.blob_bytes        },
        {", blob_reopened=",   mind.blob_reopened     },
        {", arena_bytes=",     mind.arena_bytes       },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P14 mind: ", mind_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(mind_rows));
}

/**
 * @brief The satellite run's counters on one line, followed by the codec it found.
 *
 * @param com1      Serial port the report is written to.
 * @param satellite What the run measured.
 */
static void smoke_libengine_report_power_counters(Serial_t *com1, const SatelliteReport_t *satellite)
{
    const SmokeLibengineRow_t power_rows[] = {
        {"iterations=", satellite->idle_iterations  },
        {", sleeps=",   satellite->sleeps           },
        {", skipped=",  satellite->sleeps_skipped   },
        {", halts=",    satellite->halts            },
        {", duty=",     satellite->duty_permille    },
        {", avoided=",  satellite->ticks_avoided    },
        {", monitor=",  satellite->monitor_available},
        {", scaling=",  satellite->scaling_available},
        {", refused=",  satellite->scaling_refused  },
        {", state=",    satellite->governed_state   },
        {", audio=",    satellite->audio_present    },
        {", ceiling=",  satellite->output_ceiling   },
        {", clipped=",  satellite->limiter_clipped  },
        {", peak=",     satellite->limiter_peak     },
        {", captured=", satellite->frames_captured  },
        {", level=",    satellite->capture_peak     },
    };
    smoke_libengine_write_rows(com1, "[" KERNEL_SYSTEM_STRING "]: power floor: ", power_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(power_rows));
    serial_write_string(com1, ", codec=");
    serial_write_string(com1, kernel_satellite_app_audio_name());
    serial_write_string(com1, "\n");
}

/**
 * @brief The sleep depth the processor offers, the one in force, and how often it was lowered.
 *
 * @param com1 Serial port the record is written to.
 */
static void smoke_libengine_report_sleep_depth(Serial_t *com1)
{
    kernel_telemetry_begin_record(com1, "sleep_depth_live");
    kernel_telemetry_write_unsigned("available", kernel_processor_sleep_available_hints());
    kernel_telemetry_write_unsigned("active", kernel_processor_sleep_active_hint());
    kernel_telemetry_write_unsigned("clamped", kernel_processor_sleep_clamped_count());
    kernel_telemetry_write_boolean("interrupt_break", kernel_processor_sleep_has_interrupt_break());
    kernel_telemetry_end_record();
}

/**
 * @brief Appends the capture stream's registers, read at this instant, to the open record.
 * @param line Legacy line the capture interrupt was routed on.
 */
static void smoke_libengine_write_capture_registers(uint8_t line)
{
    uint8_t stream_status = 0u;
    uint32_t interrupt_status = 0u;
    uint32_t position = 0u;
    intel_high_definition_audio_capture_registers(&stream_status, &interrupt_status, &position);

    kernel_telemetry_write_hexadecimal("stream_status", stream_status);
    kernel_telemetry_write_hexadecimal("interrupt_status", interrupt_status);
    kernel_telemetry_write_unsigned("position", position);
    kernel_telemetry_write_unsigned("pic_in_service", programmable_interrupt_controller_is_in_service(line));
}

/**
 * @brief How capture was driven and what the stream's registers say about it.
 *
 * @param com1      Serial port the record is written to.
 * @param satellite What the run measured.
 */
static void smoke_libengine_report_audio_capture(Serial_t *com1, const SatelliteReport_t *satellite)
{
    const uint8_t line = hardware_abstraction_layer_audio_capture_interrupt_line();

    kernel_telemetry_begin_record(com1, "audio_capture");
    kernel_telemetry_write_boolean("interrupt_driven", hardware_abstraction_layer_audio_capture_is_interrupt_driven());
    kernel_telemetry_write_unsigned("line", line);
    kernel_telemetry_write_unsigned("interrupts", hardware_abstraction_layer_audio_capture_interrupt_count());
    kernel_telemetry_write_unsigned("frames", satellite->frames_captured);
    kernel_telemetry_write_unsigned("overruns", hardware_abstraction_layer_audio_capture_overruns());
    smoke_libengine_write_capture_registers(line);
    kernel_telemetry_end_record();
}

/**
 * @brief The clock the processor actually delivered against the state that was requested.
 *
 * @param com1      Serial port the record is written to.
 * @param satellite What the run measured.
 */
static void smoke_libengine_report_frequency_feedback(Serial_t *com1, const SatelliteReport_t *satellite)
{
    kernel_telemetry_begin_record(com1, "frequency_feedback_live");
    kernel_telemetry_write_boolean("available", kernel_frequency_scaling_feedback_available());
    kernel_telemetry_write_unsigned("effective_permille", satellite->effective_permille);
    kernel_telemetry_write_unsigned("requested_state", satellite->governed_state);
    kernel_telemetry_end_record();
}

/**
 * @brief The audio controller, in its own words.
 *
 * @details Reported field by field because the interesting failures are partial: a controller
 *          that resets but whose codec never announces itself, and one whose codec answers,
 *          are different problems that a single boolean cannot tell apart.
 *
 * @param com1 Serial port the report is written to.
 * @param hda  The controller's bring-up state.
 */
static void smoke_libengine_report_intel_hda(Serial_t *com1, const IntelHighDefinitionAudioState_t *hda)
{
    const SmokeLibengineRow_t hda_rows[] = {
        {"present=",     (uint32_t) hda->controller_present},
        {", running=",   (uint32_t) hda->controller_running},
        {", rings=",     (uint32_t) hda->rings_running     },
        {", version=",   (uint32_t) hda->major_version     },
        {", in=",        (uint32_t) hda->input_streams     },
        {", out=",       (uint32_t) hda->output_streams    },
        {", codecs=",    (uint32_t) hda->codec_mask        },
        {", vendor0=",   hda->codec_vendor[0]              },
        {", verbs=",     hda->verbs_sent                   },
        {", answers=",   hda->responses_read               },
        {", timeouts=",  hda->verb_timeouts                },
        {", widgets=",   (uint32_t) hda->widgets_walked    },
        {", converter=", (uint32_t) hda->capture_converter },
        {", pin=",       (uint32_t) hda->capture_pin       },
        {", playpin=",   (uint32_t) hda->playback_pin      },
        {", muted=",     (uint32_t) hda->outputs_muted     },
        {", capturing=", (uint32_t) hda->capture_running   },
        {", position=",  hda->capture_position             },
        {", wraps=",     hda->capture_wraps                },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: intel-hda: ", hda_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(hda_rows));
}

/**
 * @brief The command rings either side of the first command that went unanswered.
 *
 * @details This exists because reasoning about this path produced two confident wrong answers:
 *          the registers say whether the controller ever fetched the entry, which no amount of
 *          reading the driver could settle.
 *
 * @param com1 Serial port the report is written to.
 * @param hda  The controller's bring-up state; nothing is written unless it captured a probe.
 */
static void smoke_libengine_report_intel_hda_probe(Serial_t *com1, const IntelHighDefinitionAudioState_t *hda)
{
    if (!hda->probe_captured)
        return;

    const SmokeLibengineRow_t probe_rows[] = {
        {"command=",      hda->probe_command                                       },
        {", corbwp_pre=", (uint32_t) hda->probe_before.command_write_pointer       },
        {", corbrp_pre=", (uint32_t) hda->probe_before.command_read_pointer        },
        {", rirbwp_pre=", (uint32_t) hda->probe_before.response_write_pointer      },
        {", rd_pre=",     (uint32_t) hda->probe_before.response_read_pointer_shadow},
        {", corbwp=",     (uint32_t) hda->probe_after.command_write_pointer        },
        {", shadow=",     (uint32_t) hda->probe_after.command_write_pointer_shadow },
        {", corbrp=",     (uint32_t) hda->probe_after.command_read_pointer         },
        {", rirbwp=",     (uint32_t) hda->probe_after.response_write_pointer       },
        {", rd=",         (uint32_t) hda->probe_after.response_read_pointer_shadow },
        {", corbctl=",    (uint32_t) hda->probe_after.command_ring_control         },
        {", corbsts=",    (uint32_t) hda->probe_after.command_ring_status          },
        {", rirbctl=",    (uint32_t) hda->probe_after.response_ring_control        },
        {", rirbsts=",    (uint32_t) hda->probe_after.response_ring_status         },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: intel-hda probe: ", probe_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(probe_rows));
}

/**
 * @brief The power floor, measured rather than promised.
 *
 * @details A profile whose whole purpose is to spend nothing has to report a number, or "it
 *          idles cheaply" is indistinguishable from a spin loop. What is printed is what the
 *          hardware actually did: whether it has MONITOR/MWAIT at all, how many sleeps it
 *          really took, and how much of the accounted time it was awake.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_power_floor(Serial_t *com1)
{
    SatelliteReport_t satellite;
    if (!kernel_satellite_app_run(8u, &satellite))
        return;

    smoke_libengine_report_power_counters(com1, &satellite);
    kernel_wakeup_accounting_report(com1);
    smoke_libengine_report_sleep_depth(com1);
    smoke_libengine_report_audio_capture(com1, &satellite);
    smoke_libengine_report_frequency_feedback(com1, &satellite);

    const IntelHighDefinitionAudioState_t *const hda = intel_high_definition_audio_state();
    smoke_libengine_report_intel_hda(com1, hda);
    smoke_libengine_report_intel_hda_probe(com1, hda);
}

/**
 * @brief Gate P15: one protocol, three machines that share no instruction set, one set of
 *        decisions about the same audio.
 *
 * @details The audio is synthesised from the frame index alone so no recording has to reach
 *          both sides — the same reason the world gate derives a world from a seed. Must match
 *          LplAssistant/tests/test_satellite_parity.cpp.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p15_satellite(Serial_t *com1)
{
    libassistant_satellite_fold_result_t node;
    libassistant_satellite_fold(&node);
    const SmokeLibengineRow_t node_rows[] = {
        {"feature_sig=",    node.feature_sig    },
        {", level_sig=",    node.level_sig      },
        {", event_sig=",    node.event_sig      },
        {", wire_sig=",     node.wire_sig       },
        {", state_sig=",    node.state_sig      },
        {", template_sig=", node.template_sig   },
        {", emitted=",      node.emitted        },
        {", utterances=",   node.utterances     },
        {", detections=",   node.detections     },
        {", wake_frame=",   node.wake_frame     },
        {", wake_dist=",    node.wake_distance  },
        {", speech_dist=",  node.speech_distance},
        {", echoes=",       node.echoes         },
        {", transitions=",  node.transitions    },
        {", idle=",         node.idle_permille  },
        {", duty=",         node.duty_permille  },
        {", tagged_audio=", node.tagged_audio   },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P15 satellite: ", node_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(node_rows));
}

/**
 * @brief Gate P16: one turn of thought, decided identically on both targets.
 *
 * @details P14 proves the demon COMPUTES the same thing; this proves it DECIDES the same thing —
 *          which note it kept, which move it reached for, whether it finished or asked for
 *          help. Nothing here runs a model, which is what makes it replayable at all. Must match
 *          LplAssistant/tests/test_agency_parity.cpp.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p16_agency(Serial_t *com1)
{
    libassistant_agency_fold_result_t agency;
    libassistant_agency_fold(&agency);
    const SmokeLibengineRow_t agency_rows[] = {
        {"persona_sig=",      agency.persona_sig   },
        {", intent_sig=",     agency.intent_sig    },
        {", memory_sig=",     agency.memory_sig    },
        {", recall_sig=",     agency.recall_sig    },
        {", transcript_sig=", agency.transcript_sig},
        {", utterance_sig=",  agency.utterance_sig },
        {", budget_sig=",     agency.budget_sig    },
        {", intent_kind=",    agency.intent_kind   },
        {", dropped=",        agency.dropped       },
        {", notes_held=",     agency.notes_held    },
        {", evictions=",      agency.evictions     },
        {", refusals=",       agency.refusals      },
        {", recall_hits=",    agency.recall_hits   },
        {", lines=",          agency.lines         },
        {", steps=",          agency.steps         },
        {", tokens=",         agency.tokens        },
        {", utterance_kind=", agency.utterance_kind},
        {", satisfied=",      agency.satisfied     },
        {", world_refusals=", agency.world_refusals},
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P16 agency: ", agency_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(agency_rows));
}

/**
 * @brief Gate P17: the same turn, every move chosen by the transformer running here.
 *
 * @details Under a grammar rebuilt from the world at each step. Must match
 *          LplAssistant/tests/test_agency_parity.cpp.
 *
 * @note `illegal` must be zero, and `free_legal` is what makes that mean something — the same
 *       weights generating unconstrained, and how often they land on a move the world would
 *       have accepted.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p17_reasoning(Serial_t *com1)
{
    libassistant_reasoning_fold_result_t reasoning;
    libassistant_reasoning_fold(&reasoning);
    const SmokeLibengineRow_t reasoning_rows[] = {
        {"reason_transcript_sig=",  reasoning.transcript_sig},
        {", reason_action_sig=",    reasoning.action_sig    },
        {", reason_utterance_sig=", reasoning.utterance_sig },
        {", reason_generations=",   reasoning.generations   },
        {", reason_completions=",   reasoning.completions   },
        {", reason_illegal=",       reasoning.illegal       },
        {", reason_exhausted=",     reasoning.exhausted     },
        {", reason_tokens=",        reasoning.tokens        },
        {", reason_lines=",         reasoning.lines         },
        {", reason_steps=",         reasoning.steps         },
        {", reason_satisfied=",     reasoning.satisfied     },
        {", reason_free_attempts=", reasoning.free_attempts },
        {", reason_free_legal=",    reasoning.free_legal    },
        {", reason_arena=",         reasoning.arena_bytes   },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P17 reasoning: ", reasoning_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(reasoning_rows));
}

#    endif /* !LPL_ASSISTANT_UNAVAILABLE */

#    if !defined(LPL_KNOWLEDGE_UNAVAILABLE)

/**
 * @brief Gate P18: the canonical corpus of P13, written to bytes by a host tool and read back
 *        HERE.
 *
 * @details Unlike every gate before it, what is being checked is a TRANSLATION — so
 *          `know_timeline_sig`, `know_chronicle_sig` and `know_minority_sig` must equal the P13
 *          line printed above, and `know_round_trip` must be 1. Must match
 *          LplKnowledge/tests/test_knowledge_parity.cpp.
 *
 * @note An image that opened cleanly and had rounded one confidence would pass everything else.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p18_corpus(Serial_t *com1)
{
    libknowledge_corpus_fold_result_t corpus;
    libknowledge_corpus_fold(&corpus);
    const SmokeLibengineRow_t corpus_rows[] = {
        {"know_image_sig=",       corpus.image_sig    },
        {", know_fact_sig=",      corpus.fact_sig     },
        {", know_vocab_sig=",     corpus.vocab_sig    },
        {", know_audit_sig=",     corpus.audit_sig    },
        {", know_page_sig=",      corpus.page_sig     },
        {", know_citation_sig=",  corpus.citation_sig },
        {", know_timeline_sig=",  corpus.timeline_sig },
        {", know_chronicle_sig=", corpus.chronicle_sig},
        {", know_minority_sig=",  corpus.minority_sig },
        {", know_bytes=",         corpus.image_bytes  },
        {", know_open_status=",   corpus.open_status  },
        {", know_sections=",      corpus.sections     },
        {", know_skipped=",       corpus.skipped      },
        {", know_facts=",         corpus.facts        },
        {", know_sources=",       corpus.sources      },
        {", know_documents=",     corpus.documents    },
        {", know_loci=",          corpus.loci         },
        {", know_names=",         corpus.names        },
        {", know_matched=",       corpus.matched      },
        {", know_returned=",      corpus.returned     },
        {", know_truncated=",     corpus.truncated    },
        {", know_consensus=",     corpus.consensus    },
        {", know_provenance=",    corpus.provenance   },
        {", know_round_trip=",    corpus.round_trip   },
        {", know_rejected=",      corpus.rejected     },
    };
    smoke_libengine_write_rows(com1, "[" KERNEL_SYSTEM_STRING "]: libknowledge P18 corpus: ", corpus_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(corpus_rows));
    serial_write_string(com1, ", know_image=");
    serial_write_string(com1, libknowledge_image_state());
    serial_write_string(com1, "\n");
}

#    endif /* !LPL_KNOWLEDGE_UNAVAILABLE */

/**
 * @brief Gate P19: the first gate whose subject is a world that was WALKED.
 *
 * @details Two targets agreeing about a generated cave is the easy half; what a player has is a
 *          body that entered it, and that goes through the vertical span query and the
 *          character controller as well as the generator — three Fixed32 links, each able to
 *          disagree on its own. Must match tests/parity/test_cave_warren.cpp.
 *
 * @note `enclosed` is what makes the signatures mean anything: a run in which the body never
 *       got inside folds perfectly stably on both targets and proves nothing, so the number of
 *       ticks it spent under rock is folded beside the hashes. `sealed_in` is the control —
 *       the same cave with its doorway filled with rock, which must let nobody in; a collider
 *       that let everything through would satisfy the first number and fail this one.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p19_caves(Serial_t *com1)
{
    libengine_caves_fold_result_t caves;
    libengine_caves_fold(&caves);
    const SmokeLibengineRow_t caves_rows[] = {
        {"warren_sig=",   caves.warren_sig},
        {", walk_sig=",   caves.walk_sig  },
        {", span_sig=",   caves.span_sig  },
        {", sealed_sig=", caves.sealed_sig},
        {", covered=",    caves.covered   },
        {", open=",       caves.open_cells},
        {", reachable=",  caves.reachable },
        {", aperture=",   caves.aperture  },
        {", path=",       caves.path      },
        {", enclosed=",   caves.enclosed  },
        {", descended=",  caves.descended },
        {", sealed_in=",  caves.sealed_in },
        {", navigable=",  caves.navigable },
        {", kind=",       caves.kind      },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P19 caves: ", caves_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(caves_rows));
}

/**
 * @brief Gate P21: a world whose lowest frequency is a survey rather than a generator.
 *
 * @details Must match tests/parity/test_relief_parity.cpp.
 *
 * @note `measured`, `blended` and `sea` are what make the signatures mean something: a run in
 *       which the relief was never consulted folds perfectly stably on both targets. And
 *       `plain_height` is the control — the same world with no survey behind it, which must
 *       come out DIFFERENT, or the whole path did nothing.
 *
 * @param com1 Serial port the report is written to.
 */
static void smoke_libengine_report_p21_relief(Serial_t *com1)
{
    libengine_relief_fold_result_t relief;
    libengine_relief_fold(&relief);
    const SmokeLibengineRow_t relief_rows[] = {
        {"sample_sig=",     relief.sample_sig          },
        {", height_sig=",   relief.height_sig          },
        {", walk_sig=",     relief.walk_sig            },
        {", coast_sig=",    relief.coast_sig           },
        {", measured=",     relief.measured            },
        {", invented=",     relief.invented            },
        {", blended=",      relief.blended             },
        {", sea=",          relief.sea                 },
        {", steps=",        relief.steps               },
        {", descended=",    (uint32_t) relief.descended},
        {", plain_height=", relief.plain_height        },
        {", plain_walk=",   relief.plain_walk          },
    };
    smoke_libengine_write_line(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P21 relief: ", relief_rows,
                               SMOKE_LIBENGINE_ROW_COUNT(relief_rows));
}

void smoke_libengine_run_all(Serial_t *com1)
{
    smoke_libengine_report_constructor_self_test(com1);
    smoke_libengine_report_p0_determinism(com1);
    smoke_libengine_report_p1_arena(com1);
    smoke_libengine_report_p1_ecs(com1);
    smoke_libengine_report_p1_scheduler(com1);
    smoke_libengine_report_p1_physics(com1);
    smoke_libengine_report_p2_hal(com1);
    smoke_libengine_report_p3_render(com1);
    smoke_libengine_report_p4_image(com1);
    smoke_libengine_report_p4_scene(com1);
    smoke_libengine_report_p5_render(com1);
    smoke_libengine_report_p6_advanced_rendering(com1);
    smoke_libengine_report_p4_image_present(com1);
    smoke_libengine_report_p5_render_present(com1);
    smoke_libengine_report_cube_pile_simulation(com1);
    smoke_libengine_report_p7_procedural_world(com1);
    smoke_libengine_report_p8_living_simulation(com1);
    smoke_libengine_report_p9_endless_world(com1);
    smoke_libengine_report_p10_botany(com1);
    smoke_libengine_report_p11_codec(com1);
    smoke_libengine_report_p12_rosetta(com1);
    smoke_libengine_report_p13_history(com1);
    smoke_libengine_report_p20_journey(com1);
#    if !defined(LPL_ASSISTANT_UNAVAILABLE)
    smoke_libengine_report_p14_mind(com1);
    smoke_libengine_report_power_floor(com1);
    smoke_libengine_report_p15_satellite(com1);
    smoke_libengine_report_p16_agency(com1);
    smoke_libengine_report_p17_reasoning(com1);
#    endif /* !LPL_ASSISTANT_UNAVAILABLE */
#    if !defined(LPL_KNOWLEDGE_UNAVAILABLE)
    smoke_libengine_report_p18_corpus(com1);
#    endif /* !LPL_KNOWLEDGE_UNAVAILABLE */
    smoke_libengine_report_p19_caves(com1);
    smoke_libengine_report_p21_relief(com1);
}

#endif /* LPL_KERNEL_ENABLE_SMOKE_TESTS */
