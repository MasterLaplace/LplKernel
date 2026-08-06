#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/testing/smoke_libengine.h>

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)

#    include <kernel/boot/boot_module.h>
#    include <kernel/boot/init_array.h>
#    include <kernel/hal/hal.h>

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

    /* Codec (P11): erasure coding, and the first gate whose two sides run different
       code. The host XORs 128 bits at a time, this build one word at a time, and the
       claim is that reordering associative, commutative, rounding-free operations
       changes nothing. vector_kernel says which path THIS build took, so a run where
       both sides quietly took the scalar loop is visible rather than silently green.
       Must match tests/parity/test_codec_parity.cpp. */
    {
        libengine_codec_fold_result_t codec;
        libengine_codec_fold(&codec);
        const struct {
            const char *label;
            uint32_t value;
        } codec_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P11 codec: ");
        for (size_t i = 0u; i < sizeof(codec_rows) / sizeof(codec_rows[0]); ++i)
        {
            serial_write_string(com1, codec_rows[i].label);
            serial_write_hex32(com1, codec_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* Rosetta (P12): the artifact that carries its own reader. Folds the trace of a
       canonical program on the ten-opcode ISA, the engraved specification, the plate
       around it, and self_hosting — whether a machine REBUILT from those engraved
       bytes runs the program to the same trace. That last one is the whole claim: a
       plate whose specification is not sufficient is a blob with decoration on it.
       Must match tests/parity/test_rosetta_isa.cpp. */
    {
        libengine_rosetta_fold_result_t rosetta;
        libengine_rosetta_fold(&rosetta);
        const struct {
            const char *label;
            uint32_t value;
        } rosetta_rows[] = {
            {"trace_sig=",     rosetta.trace_sig      },
            {", spec_sig=",    rosetta.spec_sig       },
            {", plate_sig=",   rosetta.plate_sig      },
            {", payload_sig=", rosetta.payload_sig    },
            {", steps=",       rosetta.steps          },
            {", halted=",      rosetta.halted         },
            {", opcodes=",     rosetta.rebuilt_opcodes},
            {", selfhost=",    rosetta.self_hosting   },
        };
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P12 rosetta: ");
        for (size_t i = 0u; i < sizeof(rosetta_rows) / sizeof(rosetta_rows[0]); ++i)
        {
            serial_write_string(com1, rosetta_rows[i].label);
            serial_write_hex32(com1, rosetta_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* History (P13): two sources, one past. A confidence in Fixed32 decides which of
       two contradictory claims becomes the consensus view, so it is authoritative
       state — a rounding that differed between targets would give two histories from
       one corpus. minority_reachable is the claim that matters most: the losing
       account must still be THERE, or how a myth was built can never be retraced.
       Must match tests/parity/test_history_parity.cpp. */
    {
        libengine_history_fold_result_t history;
        libengine_history_fold(&history);
        const struct {
            const char *label;
            uint32_t value;
        } history_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P13 history: ");
        for (size_t i = 0u; i < sizeof(history_rows) / sizeof(history_rows[0]); ++i)
        {
            serial_write_string(com1, history_rows[i].label);
            serial_write_hex32(com1, history_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

#    if !defined(LPL_ASSISTANT_UNAVAILABLE)
    /* The mind (P14): the demon thinks in ring 0, and thinks the same thing the host
       does. Everything is integer — eight-bit weights, Q16.16 activations, an
       exponential built from a shift and six Taylor terms, rotary angles from CORDIC
       — because a float forward pass would put the answer one rounding mode away
       from a different one, and every gate downstream of an answer assumes the answer
       is reproducible.

       The aperture is exercised first and the gate second, in that order and not the
       other way round: the gate re-carves the tensor arena from zero, so the live mind
       does not survive it.

       Must match LplAssistant/tests/test_infer_parity.cpp. */
    {
        const bool up = libassistant_boot(libassistant_recommended_arena_bytes());
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant boot: ");
        serial_write_string(com1, up ? "ready" : "FAILED");
        serial_write_string(com1, ", model slot=");
        serial_write_string(com1, libassistant_model_slot_state());
        serial_write_string(com1, ", arena=");
        serial_write_hex32(com1, (uint32_t) kernel_tensor_arena_size());
        serial_write_string(com1, "\n");

        if (up)
        {
            libassistant_dialogue_result_t dialogue;
            const bool answered = libassistant_dialogue_round_trip(&dialogue);
            const struct {
                const char *label;
                uint32_t value;
            } dialogue_rows[] = {
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
            serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant dialogue: ");
            for (size_t i = 0u; i < sizeof(dialogue_rows) / sizeof(dialogue_rows[0]); ++i)
            {
                serial_write_string(com1, dialogue_rows[i].label);
                serial_write_hex32(com1, dialogue_rows[i].value);
            }
            serial_write_string(com1, answered ? " (ok)\n" : " (no valid call)\n");
        }

        libassistant_mind_fold_result_t mind;
        libassistant_mind_fold(&mind);
        const struct {
            const char *label;
            uint32_t value;
        } mind_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P14 mind: ");
        for (size_t i = 0u; i < sizeof(mind_rows) / sizeof(mind_rows[0]); ++i)
        {
            serial_write_string(com1, mind_rows[i].label);
            serial_write_hex32(com1, mind_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* The power floor, measured rather than promised. A profile whose whole purpose
       is to spend nothing has to report a number, or "it idles cheaply" is
       indistinguishable from a spin loop. What is printed is what the hardware
       actually did: whether it has MONITOR/MWAIT at all, how many sleeps it really
       took, and how much of the accounted time it was awake. */
    {
        SatelliteReport_t satellite;
        if (kernel_satellite_app_run(8u, &satellite))
        {
            const struct {
                const char *label;
                uint32_t value;
            } power_rows[] = {
                {"iterations=", satellite.idle_iterations  },
                {", sleeps=",   satellite.sleeps           },
                {", skipped=",  satellite.sleeps_skipped   },
                {", halts=",    satellite.halts            },
                {", duty=",     satellite.duty_permille    },
                {", avoided=",  satellite.ticks_avoided    },
                {", monitor=",  satellite.monitor_available},
                {", scaling=",  satellite.scaling_available},
                {", refused=",  satellite.scaling_refused  },
                {", state=",    satellite.governed_state   },
                {", audio=",    satellite.audio_present    },
                {", ceiling=",  satellite.output_ceiling   },
                {", clipped=",  satellite.limiter_clipped  },
                {", peak=",     satellite.limiter_peak     },
                {", captured=", satellite.frames_captured  },
                {", level=",    satellite.capture_peak     },
            };
            serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: power floor: ");
            for (size_t i = 0u; i < sizeof(power_rows) / sizeof(power_rows[0]); ++i)
            {
                serial_write_string(com1, power_rows[i].label);
                serial_write_hex32(com1, power_rows[i].value);
            }
            serial_write_string(com1, ", codec=");
            serial_write_string(com1, kernel_satellite_app_audio_name());
            serial_write_string(com1, "\n");

            /* The controller, in its own words. Reported field by field because the
               interesting failures are partial: a controller that resets but whose
               codec never announces itself, and one whose codec answers, are
               different problems that a single boolean cannot tell apart. */
            const IntelHighDefinitionAudioState_t *const hda = intel_high_definition_audio_state();
            const struct {
                const char *label;
                uint32_t value;
            } hda_rows[] = {
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
            serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: intel-hda: ");
            for (size_t i = 0u; i < sizeof(hda_rows) / sizeof(hda_rows[0]); ++i)
            {
                serial_write_string(com1, hda_rows[i].label);
                serial_write_hex32(com1, hda_rows[i].value);
            }
            serial_write_string(com1, "\n");

            /* When a command went unanswered, the rings either side of it. This exists
               because reasoning about this path produced two confident wrong answers:
               the registers say whether the controller ever fetched the entry, which
               no amount of reading the driver could settle. */
            if (hda->probe_captured)
            {
                const struct {
                    const char *label;
                    uint32_t value;
                } probe_rows[] = {
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
                serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: intel-hda probe: ");
                for (size_t i = 0u; i < sizeof(probe_rows) / sizeof(probe_rows[0]); ++i)
                {
                    serial_write_string(com1, probe_rows[i].label);
                    serial_write_hex32(com1, probe_rows[i].value);
                }
                serial_write_string(com1, "\n");
            }
        }
    }

    /* The satellite (P15): one protocol, three machines that share no instruction
       set, one set of decisions about the same audio. The audio is synthesised from
       the frame index alone so no recording has to reach both sides — the same reason
       the world gate derives a world from a seed.
       Must match LplAssistant/tests/test_satellite_parity.cpp. */
    {
        libassistant_satellite_fold_result_t node;
        libassistant_satellite_fold(&node);
        const struct {
            const char *label;
            uint32_t value;
        } node_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P15 satellite: ");
        for (size_t i = 0u; i < sizeof(node_rows) / sizeof(node_rows[0]); ++i)
        {
            serial_write_string(com1, node_rows[i].label);
            serial_write_hex32(com1, node_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* The agency floor (P16): one turn of thought, decided identically on both
       targets. P14 proves the demon COMPUTES the same thing; this proves it DECIDES
       the same thing — which note it kept, which move it reached for, whether it
       finished or asked for help. Nothing here runs a model, which is what makes it
       replayable at all.
       Must match LplAssistant/tests/test_agency_parity.cpp. */
    {
        libassistant_agency_fold_result_t agency;
        libassistant_agency_fold(&agency);
        const struct {
            const char *label;
            uint32_t value;
        } agency_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P16 agency: ");
        for (size_t i = 0u; i < sizeof(agency_rows) / sizeof(agency_rows[0]); ++i)
        {
            serial_write_string(com1, agency_rows[i].label);
            serial_write_hex32(com1, agency_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }

    /* The demon thinks (P17): the same turn, every move chosen by the transformer
       running here, under a grammar rebuilt from the world at each step. `illegal`
       must be zero and `free_legal` is what makes that mean something — the same
       weights generating unconstrained, and how often they land on a move the world
       would have accepted.
       Must match LplAssistant/tests/test_agency_parity.cpp. */
    {
        libassistant_reasoning_fold_result_t reasoning;
        libassistant_reasoning_fold(&reasoning);
        const struct {
            const char *label;
            uint32_t value;
        } reasoning_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libassistant P17 reasoning: ");
        for (size_t i = 0u; i < sizeof(reasoning_rows) / sizeof(reasoning_rows[0]); ++i)
        {
            serial_write_string(com1, reasoning_rows[i].label);
            serial_write_hex32(com1, reasoning_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }
#    endif /* !LPL_ASSISTANT_UNAVAILABLE */

#    if !defined(LPL_KNOWLEDGE_UNAVAILABLE)
    /* The demon remembers (P18): the canonical corpus of P13, written to bytes by a host
       tool and read back HERE. Unlike every gate before it, what is being checked is a
       TRANSLATION — so `know_timeline_sig`, `know_chronicle_sig` and `know_minority_sig`
       must equal the P13 line printed above, and `know_round_trip` must be 1. An image
       that opened cleanly and had rounded one confidence would pass everything else.
       Must match LplKnowledge/tests/test_knowledge_parity.cpp. */
    {
        libknowledge_corpus_fold_result_t corpus;
        libknowledge_corpus_fold(&corpus);
        const struct {
            const char *label;
            uint32_t value;
        } corpus_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libknowledge P18 corpus: ");
        for (size_t i = 0u; i < sizeof(corpus_rows) / sizeof(corpus_rows[0]); ++i)
        {
            serial_write_string(com1, corpus_rows[i].label);
            serial_write_hex32(com1, corpus_rows[i].value);
        }
        serial_write_string(com1, ", know_image=");
        serial_write_string(com1, libknowledge_image_state());
        serial_write_string(com1, "\n");
    }
#    endif /* !LPL_KNOWLEDGE_UNAVAILABLE */

    /* Caves (P19): the first gate whose subject is a world that was WALKED. Two
       targets agreeing about a generated cave is the easy half; what a player has is
       a body that entered it, and that goes through the vertical span query and the
       character controller as well as the generator — three Fixed32 links, each able
       to disagree on its own.

       `enclosed` is what makes the signatures mean anything. A run in which the body
       never got inside folds perfectly stably on both targets and proves nothing, so
       the number of ticks it spent under rock is folded beside the hashes. `sealed_in`
       is the control: the same cave with its doorway filled with rock, which must let
       nobody in — a collider that let everything through would satisfy the first
       number and fail this one.

       Must match tests/parity/test_cave_warren.cpp. */
    {
        libengine_caves_fold_result_t caves;
        libengine_caves_fold(&caves);
        const struct {
            const char *label;
            uint32_t value;
        } caves_rows[] = {
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
        serial_write_string(com1, "[" KERNEL_SYSTEM_STRING "]: libengine P19 caves: ");
        for (size_t i = 0u; i < sizeof(caves_rows) / sizeof(caves_rows[0]); ++i)
        {
            serial_write_string(com1, caves_rows[i].label);
            serial_write_hex32(com1, caves_rows[i].value);
        }
        serial_write_string(com1, "\n");
    }
}

#endif /* LPL_KERNEL_ENABLE_SMOKE_TESTS */
