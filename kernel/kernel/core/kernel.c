#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/boot/helpers/multiboot_info_helper.h>
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
#include <kernel/cpu/idt.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/numa_policy.h>
#include <kernel/cpu/paging.h>
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

static uint8_t kernel_string_equals(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs)
        return 0u;

    while (*lhs && *rhs)
    {
        if (*lhs != *rhs)
            return 0u;
        ++lhs;
        ++rhs;
    }

    return (uint8_t) (*lhs == '\0' && *rhs == '\0');
}

static void kernel_console_print_prompt(void) { terminal_write_string("\n> "); }

static void kernel_console_execute_command(const char *command)
{
    if (!command || !*command)
        return;

    if (kernel_string_equals(command, "help"))
    {
        terminal_write_string("\ncommands: help, stats, ap, kbd, exit\n");
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: cmd help\n");
        return;
    }

    if (kernel_string_equals(command, "stats"))
    {
        terminal_write_string("\n[stats] heap strategy=");
        terminal_write_string(kernel_heap_get_strategy_name());
        terminal_write_string(", pmm free pages=");
        terminal_write_number((long) physical_memory_manager_get_free_page_count(), 10u);
        terminal_write_string("\n");
        return;
    }

    if (kernel_string_equals(command, "ap"))
    {
        terminal_write_string("\n[ap] discovered=");
        terminal_write_number((long) cpu_topology_get_discovered_cpu_count(), 10u);
        terminal_write_string(", online=");
        terminal_write_number((long) cpu_topology_get_online_cpu_count(), 10u);
        terminal_write_string(", reported=");
        terminal_write_number((long) application_processor_startup_get_reported_online_count(), 10u);
        terminal_write_string(", startup_attempts=");
        terminal_write_number((long) advanced_pic_ipi_get_startup_sequence_attempt_count(), 10u);
        terminal_write_string(", startup_success=");
        terminal_write_number((long) advanced_pic_ipi_get_startup_sequence_success_count(), 10u);
        terminal_write_string(", tramp_ack_ok=");
        terminal_write_number((long) application_processor_trampoline_get_ack_success_count(), 10u);
        terminal_write_string(", tramp_ack_to=");
        terminal_write_number((long) application_processor_trampoline_get_ack_timeout_count(), 10u);
        terminal_write_string("\n");
        return;
    }

    if (kernel_string_equals(command, "kbd"))
    {
        terminal_write_string("\n[kbd] irq=");
        terminal_write_number((long) keyboard_get_irq_count(), 10u);
        terminal_write_string(", printable=");
        terminal_write_number((long) keyboard_get_printable_count(), 10u);
        terminal_write_string(", pending=");
        terminal_write_number((long) keyboard_get_pending_char_count(), 10u);
        terminal_write_string(", dropped=");
        terminal_write_number((long) keyboard_get_dropped_char_count(), 10u);
        terminal_write_string("\n");
        return;
    }

    if (kernel_string_equals(command, "exit"))
        return;

    terminal_write_string("\nunknown command: ");
    terminal_write_string(command);
    terminal_write_string("\n");
}

static void kernel_try_start_discovered_aps(void)
{
    uint8_t ap_bootstrap_ok = application_processor_bootstrap_initialize();

    write_ap_bootstrap_init_info(&com1, ap_bootstrap_ok);

    application_processor_startup_set_serial_port(&com1);

    if (!ap_bootstrap_ok)
        return;

    if (!advanced_pic_ipi_is_ready())
    {
        write_ap_startup_skipped_ipi_not_ready_info(&com1);
        return;
    }

    if (!application_processor_startup_ensure_low_identity_mapping())
    {
        write_ap_startup_skipped_identity_map_unavailable_info(&com1);
        return;
    }

    if (!application_processor_trampoline_install())
    {
        write_ap_startup_skipped_trampoline_install_failed_info(&com1);
        return;
    }

    write_ap_trampoline_installed_info(&com1);

    application_processor_bootstrap_reset_iteration();

    uint32_t attempted = 0u;
    uint32_t delivered = 0u;
    uint32_t retries_consumed = 0u;
    uint32_t sequence_failures = 0u;
    uint32_t acknowledgement_timeouts = 0u;
    uint32_t c_entry_timeouts = 0u;
    ApplicationProcessorBootstrapEntry_t *entry = NULL;

    while ((entry = application_processor_bootstrap_next_unbooted_ap()) != NULL)
    {
        ++attempted;

        uint8_t sequence_ok = 0u;
        uint8_t ack_ok = 0u;
        uint8_t c_entry_ok = 0u;
        uint8_t attempts_used = 0u;

        for (uint32_t attempt = 0u; attempt < KERNEL_AP_STARTUP_MAX_ATTEMPTS; ++attempt)
        {
            void *ap_stack_top = application_processor_bootstrap_get_ap_stack_top(entry->logical_slot);

            if (!ap_stack_top)
            {
                ++sequence_failures;
                continue;
            }

            attempts_used = (uint8_t) (attempt + 1u);
            application_processor_trampoline_reset_acknowledgement();
            application_processor_trampoline_configure_handoff(
                entry->apic_id, entry->logical_slot, (uint32_t) (uintptr_t) ap_stack_top,
                (uint32_t) (uintptr_t) application_processor_startup_initialize_cpu,
                (uint32_t) (uintptr_t) application_processor_startup_main_loop,
                application_processor_startup_get_kernel_cr3());
            sequence_ok = advanced_pic_ipi_send_startup_sequence(entry->apic_id,
                                                                 application_processor_trampoline_get_startup_vector());
            if (!sequence_ok)
            {
                ++sequence_failures;
                continue;
            }

            ack_ok = application_processor_trampoline_wait_for_acknowledgement(KERNEL_AP_TRAMPOLINE_ACK_SPIN_LIMIT);
            if (!ack_ok)
            {
                ++acknowledgement_timeouts;
                continue;
            }

            c_entry_ok = application_processor_trampoline_wait_for_c_entry(KERNEL_AP_TRAMPOLINE_C_ENTRY_SPIN_LIMIT);
            if (!c_entry_ok)
            {
                ++c_entry_timeouts;
                continue;
            }

            break;
        }

        if (attempts_used > 1u)
            retries_consumed += (uint32_t) (attempts_used - 1u);

        if (sequence_ok && ack_ok && c_entry_ok)
        {
            ++delivered;
        }

        write_ap_startup_dispatch_info(&com1, entry, sequence_ok, ack_ok, c_entry_ok, attempts_used);
    }

    write_ap_startup_summary(&com1, attempted, delivered, retries_consumed, sequence_failures, acknowledgement_timeouts,
                             c_entry_timeouts);
}

__attribute__((constructor)) void kernel_initialize(void)
{
    serial_initialize(&com1, COM1, 9600);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: serial port initialisation successful.\n");

    terminal_initialize();

    if (!multiboot_info)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "] ERROR: Multiboot info is NULL in constructor!\n");
        return;
    }

    write_multiboot_info(&com1, KERNEL_VIRTUAL_BASE, multiboot_info);

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing GDT...\n");
    global_descriptor_table_initialize(&global_descriptor_table, &task_state_segment);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: loading GDT into CPU...\n");
    global_descriptor_table_load(&global_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: GDT loaded successfully!\n");
    write_global_descriptor_table(&com1, &global_descriptor_table);

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing IDT...\n");
    interrupt_descriptor_table_initialize(&interrupt_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: loading IDT into CPU...\n");
    interrupt_descriptor_table_load(&interrupt_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: IDT loaded successfully!\n");
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing clock policy...\n");
    clock_initialize();
    write_clock_info(&com1);

    cpu_topology_initialize();
    write_cpu_topology_info(&com1);

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing runtime paging...\n");
    paging_initialize_runtime();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: runtime paging initialized successfully!\n");
    kernel_vmm_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: kernel VMM initialized successfully!\n");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing PMM...\n");
    write_pmm_strategy_info(&com1);
    physical_memory_manager_initialize();
    write_pmm_pass_1_info(&com1);

    advanced_configuration_and_power_interface_madt_initialize();
    numa_policy_initialize();

    write_pmm_pass_2_start_info(&com1);
    physical_memory_manager_extend_mapping();
    write_pmm_ready_info(&com1);

    kernel_heap_initialize();
    write_heap_info(&com1);

    bool frame_arena_ok = kernel_frame_arena_initialize(KERNEL_FRAME_ARENA_DEFAULT_CAPACITY_BYTES);
    bool stack_allocator_ok = kernel_stack_allocator_initialize(KERNEL_STACK_ALLOCATOR_DEFAULT_CAPACITY_BYTES);
    bool pool_allocator_ok = kernel_pool_allocator_initialize(KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_SIZE,
                                                              KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_COUNT);
    bool pinned_ok = kernel_pinned_memory_initialize();
    write_core_allocators_info(&com1, frame_arena_ok, stack_allocator_ok, pool_allocator_ok, pinned_ok);

    bool ring_buffer_ok = kernel_ring_buffer_initialize_ex(
        KERNEL_RING_BUFFER_DEFAULT_SLOT_SIZE, KERNEL_RING_BUFFER_DEFAULT_SLOT_COUNT, KERNEL_RING_BUFFER_MODE_SPSC);
    write_ring_buffer_info(&com1, ring_buffer_ok);

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

    input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold();
    write_ioapic_scaffold_info(&com1);
    input_output_advanced_programmable_interrupt_controller_set_isa_route_destination(1u, 0u);

    write_ioapic_routes_info(&com1);

    if (advanced_pic_timer_backend_late_initialize())
    {
        advanced_pic_ipi_initialize(advanced_pic_timer_backend_get_local_apic_virtual_base());
        write_apic_late_init_state_info(&com1);

        kernel_try_start_discovered_aps();

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

    if (KERNEL_SMOKE_TEST_ENABLE_PAGING_PT_RECLAIM)
        kernel_smoke_test_run_paging_runtime_page_table_reclaim(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_HEAP_ALLOCATE_FREE)
    {
        kernel_smoke_test_run_heap_allocate_free(&com1);
        kernel_smoke_test_run_heap_poison_canary(&com1);
    }

    kernel_smoke_test_run_pmm_uaf_detection(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_RING3_MINIMAL)
        kernel_smoke_test_run_ring3_minimal(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_COMPACTION)
        kernel_smoke_test_run_cpu_topology_compaction(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_MADT_SYNC)
        kernel_smoke_test_run_cpu_topology_madt_sync(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_RUNTIME_SLOT)
        kernel_smoke_test_run_cpu_topology_runtime_slot(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_ONLINE_BOOKKEEPING)
        kernel_smoke_test_run_cpu_topology_online_bookkeeping(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CPU_TOPOLOGY_SLOT_DOMAIN)
        kernel_smoke_test_run_cpu_topology_slot_domain(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_SLAB_ALLOC_FREE)
        kernel_smoke_test_run_slab_alloc_free(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_CLIENT_HOT_LOOP_RULE)
        kernel_smoke_test_run_client_hot_loop_rule(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_FRAME_ARENA_BASIC)
        kernel_smoke_test_run_frame_arena_basic(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_FRAME_ARENA_BUDGET)
        kernel_smoke_test_run_frame_arena_budget(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_POOL_ALLOCATOR_BASIC)
        kernel_smoke_test_run_pool_allocator_basic(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_FRAME_POISON_CHECK)
        kernel_smoke_test_run_frame_poison_check(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_POOL_DOUBLE_FREE)
        kernel_smoke_test_run_pool_double_free(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_C7_TLSF_SOAK)
        kernel_smoke_test_run_c7_tlsf_soak(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_C7_FRAME_SIMULATION)
        kernel_smoke_test_run_c7_frame_simulation(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_C7_RING_STRESS)
        kernel_smoke_test_run_c7_ring_stress(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_C7_COMBINED_HOTLOOP)
        kernel_smoke_test_run_c7_combined_hotloop(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_STACK_ALLOCATOR_BASIC)
        kernel_smoke_test_run_stack_allocator_basic(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_ALLOCATOR_WCET_BOUND)
        kernel_smoke_test_run_allocator_wcet_bound_check(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_FRAME_BUDGET_DETERMINISM)
        kernel_smoke_test_run_frame_budget_determinism(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_RING_BUFFER_BASIC)
        kernel_smoke_test_run_ring_buffer_basic(&com1);

#if !defined(LPL_KERNEL_REAL_TIME_MODE)
    if (KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_COALESCE)
        kernel_smoke_test_run_physical_memory_manager_buddy_coalesce(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_STRESS)
        kernel_smoke_test_run_physical_memory_manager_buddy_stress(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_BUDDY_ORDER)
        kernel_smoke_test_run_physical_memory_manager_buddy_order(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_HEAP_CROSS_DOMAIN_STRESS)
        kernel_smoke_test_run_heap_cross_domain_stress(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_VMM_ALLOC_FREE)
        kernel_smoke_test_run_vmm_alloc_free(&com1);
#endif

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_WATERMARK)
        kernel_smoke_test_run_pmm_watermark(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_FRAGMENTATION)
        kernel_smoke_test_run_pmm_fragmentation(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_TLSF_BASIC)
        kernel_smoke_test_run_tlsf_basic(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_TLSF_FRAGMENTATION)
        kernel_smoke_test_run_tlsf_fragmentation(&com1);

    write_pmm_guard_info(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_IRQ_RUNTIME_STATUS)
        kernel_smoke_test_run_interrupt_request_runtime_status(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_RTC_SNAPSHOT)
        kernel_smoke_test_run_realtime_clock_snapshot(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_APIC_PERIODIC_MODE)
        kernel_smoke_test_run_apic_periodic_mode(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_APIC_READINESS)
        kernel_smoke_test_run_apic_readiness(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_IOAPIC_READINESS)
        kernel_smoke_test_run_ioapic_readiness(&com1);

    write_keyboard_runtime_info(&com1);

    if (framebuffer_init())
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: framebuffer initialized successfully!\n");
    else
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: no linear framebuffer available (text mode)\n");
}

void kernel_main(void)
{
    if (KERNEL_SMOKE_TEST_ENABLE_DIVISION_ERROR)
        kernel_smoke_test_run_division_error();
    if (KERNEL_SMOKE_TEST_ENABLE_DEBUG_EXCEPTION)
        kernel_smoke_test_run_debug_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_BREAKPOINT)
        kernel_smoke_test_run_breakpoint_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_INVALID_OPCODE)
        kernel_smoke_test_run_invalid_opcode_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_GENERAL_PROTECTION)
        kernel_smoke_test_run_general_protection_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_PAGE_FAULT)
        kernel_smoke_test_run_page_fault_exception();
    if (KERNEL_SMOKE_TEST_ENABLE_DOUBLE_FAULT)
        kernel_smoke_test_run_double_fault_exception();

    if (framebuffer_available())
    {
        if (KERNEL_SMOKE_TEST_ENABLE_GRAPHICS_DEMO)
            kernel_smoke_test_run_graphics_demo(&com1);
    }
    else
    {
        terminal_write_string(WELCOME_MESSAGE);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        terminal_write_string("[" KERNEL_SYSTEM_STRING "]: loading ...\n\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_write_string(KERNEL_CONFIG_STRING);
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    }

    static const uint8_t KEY_ECHAP = 27u;
    static const uint32_t KERNEL_CONSOLE_COMMAND_MAX = 63u;
    char command_buffer[64] = {0};
    uint32_t command_length = 0u;
    uint8_t done = 0u;

    if (!framebuffer_available())
        kernel_console_print_prompt();

    while (!done)
    {
        char key_char = 0;
        uint8_t serial_char = 0u;
        uint8_t incoming = 0u;

        if (keyboard_try_pop_char(&key_char))
            incoming = (uint8_t) key_char;
        else if (serial_try_read_char(&com1, &serial_char))
            incoming = serial_char;

        if (incoming)
        {
            if (incoming == KEY_ECHAP)
            {
                done = 1u;
                continue;
            }

            if (!framebuffer_available())
            {
                if (incoming == '\r' || incoming == '\n')
                {
                    terminal_putchar('\n');
                    command_buffer[command_length] = '\0';
                    if (command_length > 0u)
                    {
                        kernel_console_execute_command(command_buffer);
                        if (kernel_string_equals(command_buffer, "exit"))
                        {
                            done = 1u;
                            continue;
                        }
                    }

                    command_length = 0u;
                    command_buffer[0] = '\0';
                    kernel_console_print_prompt();
                    continue;
                }

                if (incoming == '\b' || incoming == 127u)
                {
                    if (command_length > 0u)
                    {
                        --command_length;
                        command_buffer[command_length] = '\0';
                        terminal_putchar('\b');
                    }
                    continue;
                }

                if (incoming >= 32u && incoming <= 126u)
                {
                    if (command_length < KERNEL_CONSOLE_COMMAND_MAX)
                    {
                        command_buffer[command_length++] = (char) incoming;
                        terminal_putchar((char) incoming);
                    }
                    continue;
                }
            }

            continue;
        }

        asm volatile("hlt");
    }
}

__attribute__((destructor)) void kernel_cleanup(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write_string("\n[" KERNEL_SYSTEM_STRING "]: exiting ... panic!\n");
}
