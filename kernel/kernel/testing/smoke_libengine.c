#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/testing/smoke_libengine.h>

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)

#    include <kernel/boot/boot_module.h>
#    include <kernel/boot/init_array.h>
#    include <kernel/hal/hal.h>

#    include <libengine/libengine.h>

void smoke_libengine_run_all(Serial_t *com1)
{
    /* Static-initialization smoke test: proves the C++ constructor machinery
       (linker.ld .ctors/.init_array + _init + kernel_run_global_constructors)
       fired before kernel_main. Required foundation for the libengine C++ module. */
    if (kernel_constructor_self_test_passed())
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: C++ constructor self-test: PASS\n");
    else
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: C++ constructor self-test: FAIL\n");

    /* P0 determinism smoke: the freestanding LplPlugin engine (libengine.a)
       computes Fixed32 / CORDIC results whose raw Q16.16 bit patterns are
       fixed by the math sources. Printed here and compared, byte-for-byte,
       against the Linux/xmake oracle to prove bit-identical cross-target math
       (the HARD determinism contract, P0 exit gate). */
    {
        libengine_p0_smoke_result_t smoke;
        libengine_p0_smoke(&smoke);
        const struct {
            const char *label;
            uint32_t value;
        } smoke_rows[] = {
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

    /* P1 memory smoke: the engine ArenaAllocator (lpl::memory) runs in-kernel
       with its slab served by kmalloc through the lpl/std/cstdlib umbrella.
       Proves the dependency-injected allocator seam (allocate/align/ownsPtr/
       reset/exhaustion). used() is fixed by the allocation sizes, so it matches
       the Linux/xmake oracle (P1 memory-DI exit criterion). */
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

    /* P1 ECS smoke: the archetype/chunk SoA storage and lock-free entity
       Registry run headless in-kernel. Each value is fixed by the deterministic
       create/destroy sequence, so it matches the Linux/xmake oracle (P1 ECS
       exit criterion). */
    {
        libengine_p1_ecs_smoke_result_t ecs;
        libengine_p1_ecs_smoke(&ecs);
        const struct {
            const char *label;
            uint32_t value;
        } ecs_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 ECS smoke: ");
        for (size_t i = 0u; i < sizeof(ecs_rows) / sizeof(ecs_rows[0]); ++i)
        {
            serial_write_string(com1, ecs_rows[i].label);
            serial_write_hex32(com1, ecs_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P1 scheduler smoke: the ECS DAG scheduler runs four systems over the
       single-threaded inline job system. The wave structure and execution order
       are deterministic, so these values match the Linux/xmake oracle. */
    {
        libengine_p1_scheduler_smoke_result_t sched;
        libengine_p1_scheduler_smoke(&sched);
        const struct {
            const char *label;
            uint32_t value;
        } sched_rows[] = {
            {"systems=",     sched.system_count  },
            {", build_ok=",  sched.build_ok      },
            {", exec_mask=", sched.exec_mask     },
            {", executed=",  sched.executed_count},
            {", first=",     sched.first_marker  },
            {", last=",      sched.last_marker   },
            {", phase_cb=",  sched.phase_cb_fired},
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 scheduler smoke: ");
        for (size_t i = 0u; i < sizeof(sched_rows) / sizeof(sched_rows[0]); ++i)
        {
            serial_write_string(com1, sched_rows[i].label);
            serial_write_hex32(com1, sched_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P1 physics smoke: one CpuPhysicsBackend gravity-integration tick over ECS
       chunk storage. The float results run on SSE (-ffp-contract=off), so the
       raw IEEE bit patterns match the Linux/xmake oracle (P1 ECS+physics gate). */
    {
        libengine_p1_physics_smoke_result_t phys;
        libengine_p1_physics_smoke(&phys);
        const struct {
            const char *label;
            uint32_t value;
        } phys_rows[] = {
            {"seeded=",      phys.entities_seeded      },
            {", stepped=",   phys.entities_stepped     },
            {", step_ok=",   phys.step_ok              },
            {", pos_y_raw=", phys.position_y_raw       },
            {", vel_y_raw=", phys.velocity_y_raw       },
            {", fell_ok=",   phys.fell_under_gravity_ok},
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 physics smoke: ");
        for (size_t i = 0u; i < sizeof(phys_rows) / sizeof(phys_rows[0]); ++i)
        {
            serial_write_string(com1, phys_rows[i].label);
            serial_write_hex32(com1, phys_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P2 HAL smoke: the engine platform backends (lpl::platform) run through
       their kernel HAL implementations — display surface query/clear/readback,
       the clock tick/timestamp contract, the input ring drain, and a pinned
       graphics-memory allocate/translate/free. Proves the kernel platform seam
       is wired end to end (P2 HAL bring-up gate). These values are observability
       only (surface geometry is QEMU-config dependent; clock is wall-clock),
       NOT part of the bit-identical determinism contract. */
    {
        libengine_p2_hal_smoke_result_t hal;
        libengine_p2_hal_smoke(&hal);
        const struct {
            const char *label;
            uint32_t value;
        } hal_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P2 HAL smoke: ");
        for (size_t i = 0u; i < sizeof(hal_rows) / sizeof(hal_rows[0]); ++i)
        {
            serial_write_string(com1, hal_rows[i].label);
            serial_write_hex32(com1, hal_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P3 render smoke: KernelDisplayRenderer software-rasterises a rotating
       Fixed32-authored triangle over the IDisplayBackend HAL. Runs whenever a
       presentable surface exists — the software LFB or a virtio-gpu scanout —
       and is skipped only on a pure text-mode boot. */
    if (hardware_abstraction_layer_display_available())
    {
        libengine_p3_render_smoke_result_t p3;
        libengine_p3_render_smoke(&p3);
        const struct {
            const char *label;
            uint32_t value;
        } p3_rows[] = {
            {"display_ok=",  p3.display_available},
            {", init_ok=",   p3.renderer_init_ok },
            {", frames=",    p3.frames_rendered  },
            {", ticks=",     p3.ticks_elapsed    },
            {", centre_px=", p3.centre_pixel_raw },
            {", visible=",   p3.triangle_visible },
            {", smoke_ok=",  p3.smoke_ok         },
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P3 render smoke: ");
        for (size_t i = 0u; i < sizeof(p3_rows) / sizeof(p3_rows[0]); ++i)
        {
            serial_write_string(com1, p3_rows[i].label);
            serial_write_hex32(com1, p3_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P4 image smoke: the portable lpl::image module run in-kernel; its
       signature must match the Linux oracle (tests/test-image-parity). */
    {
        libengine_p4_image_smoke_result_t img;
        libengine_p4_image_smoke(&img);
        const struct {
            const char *label;
            uint32_t value;
        } img_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P4 image smoke: ");
        for (size_t i = 0u; i < sizeof(img_rows) / sizeof(img_rows[0]); ++i)
        {
            serial_write_string(com1, img_rows[i].label);
            serial_write_hex32(com1, img_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P4 scene smoke: the deterministic 2D scene graph (Fixed32 transforms +
       undo/redo + selection); raw values must match the Linux oracle. */
    {
        libengine_p4_scene_smoke_result_t scn;
        libengine_p4_scene_smoke(&scn);
        const struct {
            const char *label;
            uint32_t value;
        } scn_rows[] = {
            {"world_tx=",   scn.world_tx_raw        },
            {", world_ty=", scn.world_ty_raw        },
            {", undo_tx=",  scn.undo_tx_raw         },
            {", redo_tx=",  scn.redo_tx_raw         },
            {", sel=",      scn.selection           },
            {", rot_x=",    (uint32_t) scn.rot_x_raw},
            {", rot_y=",    (uint32_t) scn.rot_y_raw},
            {", scene_ok=", scn.scene_ok            },
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P4 scene smoke: ");
        for (size_t i = 0u; i < sizeof(scn_rows) / sizeof(scn_rows[0]); ++i)
        {
            serial_write_string(com1, scn_rows[i].label);
            serial_write_hex32(com1, scn_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P5 render smoke: project a Fixed32-authored unit cube (CORDIC rotation)
       through a perspective camera; the folded screen/depth signatures must
       match the Linux oracle (tests/test-render-parity) bit-for-bit. */
    {
        libengine_p5_render_smoke_result_t rnd;
        libengine_p5_render_smoke(&rnd);
        const struct {
            const char *label;
            uint32_t value;
        } rnd_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P5 render smoke: ");
        for (size_t i = 0u; i < sizeof(rnd_rows) / sizeof(rnd_rows[0]); ++i)
        {
            serial_write_string(com1, rnd_rows[i].label);
            serial_write_hex32(com1, rnd_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P6 smoke: advanced rendering (topology, software ray tracing, metallic/
       roughness PBR + HDRI tone map, immutable command buffers with
       Late-Latching, foveated rasterization). Every signature must match the
       Linux oracle (tests/test-p6-parity) bit-for-bit. */
    {
        libengine_p6_smoke_result_t p6;
        libengine_p6_smoke(&p6);
        const struct {
            const char *label;
            uint32_t value;
        } p6_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P6 smoke: ");
        for (size_t i = 0u; i < sizeof(p6_rows) / sizeof(p6_rows[0]); ++i)
        {
            serial_write_string(com1, p6_rows[i].label);
            serial_write_hex32(com1, p6_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P4 image present: paint a 2D scene and blit it onto the display scanout
       through the HAL (Image -> hardware_abstraction_layer_display -> virtio-gpu/LFB). Runs last so the
       2D scene is what stays on screen. */
    if (hardware_abstraction_layer_display_available())
    {
        libengine_p4_image_present_smoke_result_t present;
        libengine_p4_image_present_smoke(&present);
        const struct {
            const char *label;
            uint32_t value;
        } present_rows[] = {
            {"display=",      present.display_available},
            {", width=",      present.width            },
            {", height=",     present.height           },
            {", img_sig=",    present.image_signature  },
            {", present_ok=", present.present_ok       },
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P4 image present: ");
        for (size_t i = 0u; i < sizeof(present_rows) / sizeof(present_rows[0]); ++i)
        {
            serial_write_string(com1, present_rows[i].label);
            serial_write_hex32(com1, present_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* P5 render present: rasterize the depth-buffered 3D cube and present a
       scaled copy onto the scanout. The offscreen fold must match the oracle. */
    if (hardware_abstraction_layer_display_available())
    {
        libengine_p5_render_present_result_t r3d;
        libengine_p5_render_present_smoke(&r3d);
        const struct {
            const char *label;
            uint32_t value;
        } r3d_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P5 render present: ");
        for (size_t i = 0u; i < sizeof(r3d_rows) / sizeof(r3d_rows[0]); ++i)
        {
            serial_write_string(com1, r3d_rows[i].label);
            serial_write_hex32(com1, r3d_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* Sample simulation (CubePile): deterministic N-entity gravity/bounce +
       AABB collision + cube rasterization; state + image folds must match the
       Linux oracle (tests/parity) bit-for-bit. */
    {
        libengine_sim_fold_result_t sim;
        libengine_sim_fold(&sim);
        const struct {
            const char *label;
            uint32_t value;
        } sim_rows[] = {
            {"state8=",    sim.state_sig_8 },
            {", image8=",  sim.image_sig_8 },
            {", state64=", sim.state_sig_64},
            {", image64=", sim.image_sig_64},
            {", sim_ok=",  sim.sim_ok      },
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine sim (cube pile): ");
        for (size_t i = 0u; i < sizeof(sim_rows) / sizeof(sim_rows[0]); ++i)
        {
            serial_write_string(com1, sim_rows[i].label);
            serial_write_hex32(com1, sim_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* Procedural world (P7): bake a seed into a real ECS world — terrain,
       erosion, drainage and rivers, climate and biomes, blue-noise scatter, a
       cellular cave and a settlement — and fold the grids AND the authoritative
       Fixed32 state. Must match tests/parity/test_world_recipe.cpp bit-for-bit;
       this is what lets the kernel replay a world by seed rather than by parsing
       a .lplscene in ring 0. */
    {
        libengine_procgen_fold_result_t world;
        /* Prefer a cartridge: if GRUB loaded a .lplpak module alongside the
           kernel, that is the game to build. With no cartridge the built-in
           reference pack keeps the parity gate meaningful. */
        const uint8_t *cartridge = NULL;
        uint32_t cartridge_size = 0u;
        /* "game.lplpak" and not ".lplpak": the ISO now carries a second cartridge
           for the world viewer, and matching on the bare extension would hand the
           gate whichever module GRUB listed first — folding a world the oracle
           never baked, intermittently, depending on module order. */
        if (boot_module_find("game.lplpak", &cartridge, &cartridge_size))
            libengine_procgen_fold_from(cartridge, cartridge_size, &world);
        else
            libengine_procgen_fold(&world);
        const struct {
            const char *label;
            uint32_t value;
        } world_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P7 procgen world: ");
        for (size_t i = 0u; i < sizeof(world_rows) / sizeof(world_rows[0]); ++i)
        {
            serial_write_string(com1, world_rows[i].label);
            serial_write_hex32(com1, world_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* Living simulation (P8): the fold P7 cannot make. P7 proves the world's
       SHAPE crosses targets intact and then stops, because a recipe's last pass
       is the last thing it can see — everything ai/ and ecology/ do happens
       afterwards. This runs a trophic web, a breeding population, a pheromone
       field with agents on it, a flock, an abstract world under a realisation
       budget and the pack life cycle for a fixed number of ticks, and folds all
       four. Must match tests/parity/test_living_parity.cpp bit-for-bit. */
    {
        libengine_living_fold_result_t living;
        libengine_living_fold(&living);
        const struct {
            const char *label;
            uint32_t value;
        } living_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P8 living sim: ");
        for (size_t i = 0u; i < sizeof(living_rows) / sizeof(living_rows[0]); ++i)
        {
            serial_write_string(com1, living_rows[i].label);
            serial_write_hex32(com1, living_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* Endless world (P9): the third gate. P7 folds the world a recipe BUILDS,
       P8 the simulation that RUNS on it, and this one the world that STREAMS —
       terrain at absolute coordinates, rivers from a bounded basin and a coarse
       trunk level. The seam count is folded alongside the signatures because two
       targets agreeing on a seamed world would pass a signature check every
       time. Must match tests/parity/test_procgen_chunking.cpp. */
    {
        libengine_endless_fold_result_t endless;
        libengine_endless_fold(&endless);
        const struct {
            const char *label;
            uint32_t value;
        } endless_rows[] = {
            {"height_sig=",  endless.height_sig     },
            {", river_sig=", endless.river_sig      },
            {", chunks=",    endless.chunks         },
            {", river=",     endless.river_cells    },
            {", seams=",     endless.seam_mismatches},
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P9 endless world: ");
        for (size_t i = 0u; i < sizeof(endless_rows) / sizeof(endless_rows[0]); ++i)
        {
            serial_write_string(com1, endless_rows[i].label);
            serial_write_hex32(com1, endless_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* Botany (P10): the shape a grammar grows. Folded because the 3D turtle is
       CORDIC end to end — the same rotations the camera basis and the terrain
       noise are built from. Must match tests/parity/test_botany_parity.cpp. */
    {
        libengine_botany_fold_result_t botany;
        libengine_botany_fold(&botany);
        const struct {
            const char *label;
            uint32_t value;
        } botany_rows[] = {
            {"conifer_sig=",     botany.conifer_sig     },
            {", broadleaf_sig=", botany.broadleaf_sig   },
            {", shrub_sig=",     botany.shrub_sig       },
            {", segments=",      botany.conifer_segments},
            {", leaves=",        botany.conifer_leaves  },
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P10 botany: ");
        for (size_t i = 0u; i < sizeof(botany_rows) / sizeof(botany_rows[0]); ++i)
        {
            serial_write_string(com1, botany_rows[i].label);
            serial_write_hex32(com1, botany_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }
}

#endif /* LPL_KERNEL_ENABLE_SMOKE_TESTS */
