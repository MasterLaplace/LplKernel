#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/boot/helpers/multiboot_info_helper.h>
#include <kernel/boot/init_array.h>
#include <kernel/cpu/acpi.h>
#include <kernel/cpu/ap_bootstrap.h>
#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/ap_trampoline.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/helpers/acpi_helper.h>
#include <kernel/cpu/helpers/ap_startup_helper.h>
#include <kernel/cpu/helpers/apic_timer_helper.h>
#include <kernel/cpu/helpers/clock_helper.h>
#include <kernel/cpu/helpers/cpu_topology_helper.h>
#include <kernel/cpu/helpers/gdt_helper.h>
#include <kernel/cpu/helpers/ioapic_helper.h>
#include <kernel/cpu/helpers/pci_helper.h>
#include <kernel/cpu/idt.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/numa_policy.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pci.h>
#include <kernel/hal/hal.h>
#include <kernel/cpu/pic.h>
#include <kernel/cpu/pmm.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/drivers/helpers/keyboard_helper.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/ps2_keyboard.h>
#include <kernel/memory/frame_arena.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/helpers/core_allocators_helper.h>
#include <kernel/memory/helpers/heap_helper.h>
#include <kernel/memory/helpers/pmm_helper.h>
#include <kernel/memory/pinned_memory.h>
#include <kernel/memory/pool_allocator.h>
#include <kernel/memory/ring_buffer.h>
#include <kernel/memory/slab.h>
#include <kernel/memory/stack_allocator.h>
#include <kernel/memory/tlsf.h>
#include <kernel/memory/vmm.h>
#include <kernel/smoke_test.h>

#include <kernel/core/kernel_console.h>
#include <kernel/core/kernel_smoke_batch.h>
#include <kernel/core/kernel_smp.h>
#include <kernel/core/kernel_splash.h>

#include <libengine/libengine.h>

#define KERNEL_FRAME_ARENA_DEFAULT_CAPACITY_BYTES     16384u
#define KERNEL_STACK_ALLOCATOR_DEFAULT_CAPACITY_BYTES 16384u
#define KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_SIZE     64u
#define KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_COUNT    128u
#define KERNEL_RING_BUFFER_DEFAULT_SLOT_SIZE          32u
#define KERNEL_RING_BUFFER_DEFAULT_SLOT_COUNT         256u
#define KERNEL_AP_TRAMPOLINE_ACK_SPIN_LIMIT           200000u
#define KERNEL_AP_TRAMPOLINE_C_ENTRY_SPIN_LIMIT       300000u
#define KERNEL_AP_STARTUP_MAX_ATTEMPTS                3u

static const char WELCOME_MESSAGE[] = ""
                                      "/==+--  _                                         ---+\n"
                                      "| \\|   | |                  _                        |\n"
                                      "+  |   | |     __ _  ____  | |    __ _  ____  ___    |\n"
                                      "   |   | |    / _` ||  _ \\ | |   / _` |/ ___\\/ _ \\   |\n"
                                      "   |   | |___| (_| || |_| || |__| (_| | (___|  __/   |\\\n"
                                      "   |   \\_____|\\__,_|| ___/ \\____|\\__,_|\\____/\\___|   ||\n"
                                      "   |                | |_                             | \\\n"
                                      "   +---             |___|                          --+==+\n\n";

extern MultibootInfo_t *multiboot_info;

GlobalDescriptorTable_t global_descriptor_table = {0};
InterruptDescriptorTable_t interrupt_descriptor_table = {0};
static TaskStateSegment_t task_state_segment = {0};
Serial_t com1;

static uint8_t kernel_policy_enable_apic_timer_owner(void)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    return 1u;
#else
    return 0u;
#endif
}

static uint8_t kernel_policy_enable_ioapic_keyboard_owner(void)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    return 1u;
#else
    return 0u;
#endif
}

/**
 * @brief Main kernel initialization function, called as a constructor before main().
 *
 * Initialization sequence:
 * - Initialize serial port for early debug output.
 * - Print welcome message and Multiboot info for debugging.
 * - Initialize the Global Descriptor Table (GDT) and load it into the CPU.
 * - Initialize the Interrupt Descriptor Table (IDT) and load it into the CPU.
 * - Initialize the system clock and print its info.
 * - Initialize CPU topology and print its info.
 * - Initialize runtime paging and the kernel virtual memory manager (VMM).
 * - Initialize the Physical Memory Manager (PMM) and print initial info.
 * - Initialize ACPI MADT and NUMA policy, then print their info.
 * - Extend PMM mapping to cover all available physical memory and print info.
 * - Initialize the kernel heap and print its info.
 * - Initialize core allocators (frame arena, stack allocator, pool allocator, pinned memory) and print their info.
 * - Initialize the kernel ring buffer and print its info.
 * - Initialize the APIC timer if the policy allows, then print its state.
 * - Start discovered Application Processors (APs) and print their state.
 * - Initialize the IOAPIC keyboard route if the policy allows, then print its state.
 * - Run smoke tests and give control to the user.
 */
__attribute__((constructor)) void kernel_initialize(void)
{
    serial_initialize(&com1, COM1, 9600);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: serial port initialisation successful.\n");

    terminal_initialize();

    kernel_splash_initialize(17);
    kernel_splash_update("Initializing Serial COM1 (9600 baud)");

    if (!multiboot_info)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "] ERROR: Multiboot info is NULL in constructor!\n");
        return;
    }

    write_multiboot_info(&com1, KERNEL_VIRTUAL_BASE, multiboot_info);
    kernel_splash_update("Parsing Multiboot Structure");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing GDT...\n");
    global_descriptor_table_initialize(&global_descriptor_table, &task_state_segment);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: loading GDT into CPU...\n");
    global_descriptor_table_load(&global_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: GDT loaded successfully!\n");
    write_global_descriptor_table(&com1, &global_descriptor_table);
    kernel_splash_update("GDT & Task State Segment");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing IDT...\n");
    interrupt_descriptor_table_initialize(&interrupt_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: loading IDT into CPU...\n");
    interrupt_descriptor_table_load(&interrupt_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: IDT loaded successfully!\n");
    kernel_splash_update("Interrupt Descriptor Table");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing clock policy...\n");
    clock_initialize();
    write_clock_info(&com1);
    kernel_splash_update("Hardware Clock Timer");

    cpu_topology_initialize();
    write_cpu_topology_info(&com1);
    kernel_splash_update("CPU Topology & MP Spec");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing runtime paging...\n");
    paging_initialize_runtime();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: runtime paging initialized successfully!\n");
    kernel_vmm_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: kernel VMM initialized successfully!\n");
    kernel_splash_update("Runtime Paging & VMM");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing PMM...\n");
    write_pmm_strategy_info(&com1);
    physical_memory_manager_initialize();
    write_pmm_pass_1_info(&com1);
    kernel_splash_update("Physical Memory Manager Pass 1");

    advanced_configuration_and_power_interface_madt_initialize();
    numa_policy_initialize();
    kernel_splash_update("ACPI MADT Discovery");

    write_pmm_pass_2_start_info(&com1);
    physical_memory_manager_extend_mapping();
    write_pmm_ready_info(&com1);
    kernel_splash_update("Physical Memory Manager Pass 2");

    kernel_heap_initialize();
    write_heap_info(&com1);
    kernel_splash_update("Kernel Dynamic Heap");

    bool frame_arena_ok = kernel_frame_arena_initialize(KERNEL_FRAME_ARENA_DEFAULT_CAPACITY_BYTES);
    bool stack_allocator_ok = kernel_stack_allocator_initialize(KERNEL_STACK_ALLOCATOR_DEFAULT_CAPACITY_BYTES);
    bool pool_allocator_ok = kernel_pool_allocator_initialize(KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_SIZE,
                                                              KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_COUNT);
    bool pinned_ok = kernel_pinned_memory_initialize();
    write_core_allocators_info(&com1, frame_arena_ok, stack_allocator_ok, pool_allocator_ok, pinned_ok);

    bool ring_buffer_ok = kernel_ring_buffer_initialize_ex(
        KERNEL_RING_BUFFER_DEFAULT_SLOT_SIZE, KERNEL_RING_BUFFER_DEFAULT_SLOT_COUNT, KERNEL_RING_BUFFER_MODE_SPSC);
    write_ring_buffer_info(&com1, ring_buffer_ok);
    kernel_splash_update("Core Allocators (Slab, Pool, Ring)");

    write_heap_extended_info(&com1);

    write_pmm_buddy_info(&com1);

    advanced_configuration_and_power_interface_madt_initialize();
    write_acpi_madt_info(&com1);

    if (advanced_configuration_and_power_interface_madt_is_available())
    {
        for (uint8_t lapic_index = 0u;
             lapic_index < advanced_configuration_and_power_interface_madt_get_local_apic_count(); ++lapic_index)
        {
            if (!advanced_configuration_and_power_interface_madt_is_local_apic_enabled(lapic_index))
                continue;

            (void) cpu_topology_register_discovered_apic_id(
                advanced_configuration_and_power_interface_madt_get_local_apic_id(lapic_index));
        }

        write_cpu_topology_madt_sync_info(&com1);
    }

    write_acpi_ioapics_info(&com1);

    write_acpi_isos_info(&com1);

    write_acpi_isa_routing_info(&com1);

    kernel_splash_update("ACPI ISA Routing Scaffold");
    input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold();
    write_ioapic_scaffold_info(&com1);
    input_output_advanced_programmable_interrupt_controller_set_isa_route_destination(1u, 0u);

    write_ioapic_routes_info(&com1);
    kernel_splash_update("IOAPIC Dynamic Routing & Routes");

    if (advanced_pic_timer_backend_late_initialize())
    {
        advanced_pic_ipi_initialize(advanced_pic_timer_backend_get_local_apic_virtual_base());
        write_apic_late_init_state_info(&com1);

        kernel_smp_try_start_discovered_aps(&com1);

        if (kernel_policy_enable_ioapic_keyboard_owner())
        {
            if (input_output_advanced_programmable_interrupt_controller_enable_isa_route(1u))
            {
                programmable_interrupt_controller_set_mask(1u);
                interrupt_request_set_keyboard_owner_is_apic(1u);

                write_ioapic_keyboard_handoff_info(&com1, 1u);
            }
            else
            {
                write_ioapic_keyboard_handoff_info(&com1, 0u);
            }
        }
        else
        {
            write_ioapic_keyboard_policy_fallback_info(&com1);
        }

        if (advanced_pic_timer_backend_calibrate_with_pit())
        {
            write_apic_calibration_info(&com1);

            if (kernel_policy_enable_apic_timer_owner())
            {
                if (advanced_pic_timer_backend_enable_periodic_mode(interrupt_request_get_timer_frequency_hz()))
                {
                    write_apic_owner_handoff_info(&com1, 1u);
                }
                else
                {
                    write_apic_owner_handoff_info(&com1, 0u);
                }
            }
            else
            {
                write_apic_owner_policy_fallback_info(&com1);
            }
        }
        else
        {
            write_apic_calibration_skipped_info(&com1);
        }
    }
    else
    {
        write_apic_late_init_skipped_info(&com1);
    }
    kernel_splash_update("Symmetric Multiprocessing & Timers");

    kernel_smoke_batch_run_initialization_tests(&com1);
    kernel_splash_update("System Smoke Target Executions");

    peripheral_component_interconnect_scan();
    write_peripheral_component_interconnect_info(&com1);
    kernel_splash_update("PCI Bus Enumeration");

    /* VirtIO-GPU discovery (P4): locate a virtio-gpu function + its MMIO BAR so
       a later display backend can map it. Software-LFB remains the fallback. */
    {
        hal_virtio_gpu_info_t vgpu;
        if (hal_virtio_gpu_probe(&vgpu))
        {
            const struct {
                const char *label;
                uint32_t value;
            } vgpu_rows[] = {
                {"present=1 device_id=", (uint32_t) vgpu.device_id},
                {", modern=",            (uint32_t) vgpu.is_modern},
                {", bus=",               (uint32_t) vgpu.bus      },
                {", slot=",              (uint32_t) vgpu.device   },
                {", mmio_base=",         vgpu.mmio_base           },
                {", mmio_size=",         vgpu.mmio_size           },
            };
            serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: virtio-gpu probe: ");
            for (size_t i = 0u; i < sizeof(vgpu_rows) / sizeof(vgpu_rows[0]); ++i)
            {
                serial_write_string(&com1, vgpu_rows[i].label);
                serial_write_hex32(&com1, vgpu_rows[i].value);
            }
            serial_write_string(&com1, "\n");

            /* Walk the virtio-pci capability list and map the MMIO window so a
               later display backend can drive the device through its cfg
               structures. */
            hal_virtio_gpu_mapping_t vgpu_map;
            if (hal_virtio_gpu_map(&vgpu, &vgpu_map))
            {
                const struct {
                    const char *label;
                    uint32_t value;
                } map_rows[] = {
                    {"mmio_va=",        vgpu_map.mmio_virtual_base       },
                    {", bar=",          (uint32_t) vgpu_map.mmio_bar_index},
                    {", size=",         vgpu_map.mmio_size               },
                    {", common@",       vgpu_map.common.offset           },
                    {", notify@",       vgpu_map.notify.offset           },
                    {", notify_mult=",  vgpu_map.notify_off_multiplier   },
                    {", isr@",          vgpu_map.isr.offset              },
                    {", device@",       vgpu_map.device.offset           },
                };
                serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: virtio-gpu map: ");
                for (size_t i = 0u; i < sizeof(map_rows) / sizeof(map_rows[0]); ++i)
                {
                    serial_write_string(&com1, map_rows[i].label);
                    serial_write_hex32(&com1, map_rows[i].value);
                }
                serial_write_string(&com1, "\n");
            }
            else
            {
                serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: virtio-gpu map: failed (no common cfg / MMIO map)\n");
            }
        }
        else
        {
            serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: virtio-gpu probe: present=0 (software-LFB only)\n");
        }
    }

    write_keyboard_runtime_info(&com1);

    if (framebuffer_init())
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: framebuffer initialized successfully!\n");
    else
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: no linear framebuffer available (text mode)\n");
}

void kernel_main(void)
{
    /* Static-initialization smoke test: proves the C++ constructor machinery
       (linker.ld .ctors/.init_array + _init + kernel_run_global_constructors)
       fired before kernel_main. Required foundation for the libengine C++ module. */
    if (kernel_constructor_self_test_passed())
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: C++ constructor self-test: PASS\n");
    else
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: C++ constructor self-test: FAIL\n");

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
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: libengine P0 determinism smoke (raw Q16.16):\n");
        for (size_t i = 0u; i < sizeof(smoke_rows) / sizeof(smoke_rows[0]); ++i)
        {
            serial_write_string(&com1, smoke_rows[i].label);
            serial_write_hex32(&com1, smoke_rows[i].value);
            serial_write_string(&com1, "\n");
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
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 arena smoke: aligned_ok=");
        serial_write_hex32(&com1, arena.allocations_aligned_ok);
        serial_write_string(&com1, ", owns_ok=");
        serial_write_hex32(&com1, arena.owns_pointer_ok);
        serial_write_string(&com1, ", used=");
        serial_write_hex32(&com1, arena.used_after_allocations_bytes);
        serial_write_string(&com1, ", reset_ok=");
        serial_write_hex32(&com1, arena.reset_reclaims_all_ok);
        serial_write_string(&com1, ", exhaustion_null_ok=");
        serial_write_hex32(&com1, arena.exhaustion_returns_null_ok);
        serial_write_string(&com1, "\n");
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
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 ECS smoke: ");
        for (size_t i = 0u; i < sizeof(ecs_rows) / sizeof(ecs_rows[0]); ++i)
        {
            serial_write_string(&com1, ecs_rows[i].label);
            serial_write_hex32(&com1, ecs_rows[i].value);
        }
        serial_write_string(&com1, "\n");
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
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 scheduler smoke: ");
        for (size_t i = 0u; i < sizeof(sched_rows) / sizeof(sched_rows[0]); ++i)
        {
            serial_write_string(&com1, sched_rows[i].label);
            serial_write_hex32(&com1, sched_rows[i].value);
        }
        serial_write_string(&com1, "\n");
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
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: libengine P1 physics smoke: ");
        for (size_t i = 0u; i < sizeof(phys_rows) / sizeof(phys_rows[0]); ++i)
        {
            serial_write_string(&com1, phys_rows[i].label);
            serial_write_hex32(&com1, phys_rows[i].value);
        }
        serial_write_string(&com1, "\n");
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
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: libengine P2 HAL smoke: ");
        for (size_t i = 0u; i < sizeof(hal_rows) / sizeof(hal_rows[0]); ++i)
        {
            serial_write_string(&com1, hal_rows[i].label);
            serial_write_hex32(&com1, hal_rows[i].value);
        }
        serial_write_string(&com1, "\n");
    }

    /* P3 render smoke: KernelDisplayRenderer software-rasterises a rotating
       Fixed32-authored triangle over the IDisplayBackend HAL. Skipped silently
       when no framebuffer is available (text-mode boot). */
    if (framebuffer_available())
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
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: libengine P3 render smoke: ");
        for (size_t i = 0u; i < sizeof(p3_rows) / sizeof(p3_rows[0]); ++i)
        {
            serial_write_string(&com1, p3_rows[i].label);
            serial_write_hex32(&com1, p3_rows[i].value);
        }
        serial_write_string(&com1, "\n");
    }

    /* Engine boot facade: the single real entry point the engine exposes to the
       kernel. Driven bounded (max_frames=5) here so the boot continues into the
       post-boot smoke batch; a production boot would pass max_frames=0 and let
       the engine own the main loop. */
    if (framebuffer_available())
    {
        lplplugin_boot_info_t boot = {.abi_version = LPLPLUGIN_BOOT_ABI_VERSION, .max_frames = 5u};
        lplplugin_boot_result_t boot_res;
        const int boot_rc = lplplugin_initialize(&boot, &boot_res);
        const struct {
            const char *label;
            uint32_t value;
        } boot_rows[] = {
            {"rc=",          (uint32_t) boot_rc        },
            {", abi_ok=",    boot_res.abi_ok           },
            {", platform=",  boot_res.platform_ok      },
            {", display=",   boot_res.display_available},
            {", init_ok=",   boot_res.renderer_init_ok },
            {", frames=",    boot_res.frames_rendered  },
            {", shutdown=",  boot_res.shutdown_clean   },
        };
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: lplplugin_initialize: ");
        for (size_t i = 0u; i < sizeof(boot_rows) / sizeof(boot_rows[0]); ++i)
        {
            serial_write_string(&com1, boot_rows[i].label);
            serial_write_hex32(&com1, boot_rows[i].value);
        }
        serial_write_string(&com1, "\n");
    }

    if (framebuffer_available())
    {
        kernel_splash_finish();
        kernel_smoke_batch_run_post_boot_tests(&com1);
    }
    else
    {
        kernel_splash_finish();
        terminal_write_string(WELCOME_MESSAGE);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        terminal_write_string("[" KERNEL_SYSTEM_STRING "]: loading ...\n\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_write_string(KERNEL_CONFIG_STRING);
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    }

    kernel_console_run_interactive_loop(&com1);
}

__attribute__((destructor)) void kernel_cleanup(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write_string("\n[" KERNEL_SYSTEM_STRING "]: exiting ... panic!\n");
}
