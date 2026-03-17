#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/boot/multiboot_info_helper.h>
#include <kernel/cpu/acpi.h>
#include <kernel/cpu/ap_bootstrap.h>
#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/ap_trampoline.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/gdt_helper.h>
#include <kernel/cpu/idt.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pic.h>
#include <kernel/cpu/pmm.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/ps2_keyboard.h>
#include <kernel/mm/frame_arena.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pinned_memory.h>
#include <kernel/mm/pool_allocator.h>
#include <kernel/mm/ring_buffer.h>
#include <kernel/mm/slab.h>
#include <kernel/mm/stack_allocator.h>
#include <kernel/mm/tlsf.h>
#include <kernel/mm/vmm.h>
#include <kernel/smoke_test.h>

#define KERNEL_FRAME_ARENA_DEFAULT_CAPACITY_BYTES 16384u
#define KERNEL_STACK_ALLOCATOR_DEFAULT_CAPACITY_BYTES 16384u
#define KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_SIZE 64u
#define KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_COUNT 128u
#define KERNEL_RING_BUFFER_DEFAULT_SLOT_SIZE 32u
#define KERNEL_RING_BUFFER_DEFAULT_SLOT_COUNT 256u
#define KERNEL_AP_TRAMPOLINE_ACK_SPIN_LIMIT 200000u
#define KERNEL_AP_TRAMPOLINE_C_ENTRY_SPIN_LIMIT 300000u
#define KERNEL_AP_STARTUP_MAX_ATTEMPTS 3u

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

static void kernel_console_print_prompt(void)
{
    terminal_write_string("\n> ");
}

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

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: AP bootstrap init=");
    serial_write_int(&com1, (int32_t) ap_bootstrap_ok);
    serial_write_string(&com1, ", ap_count=");
    serial_write_int(&com1, (int32_t) application_processor_bootstrap_get_ap_count());
    serial_write_string(&com1, ", kernel_cr3=");
    serial_write_hex32(&com1, application_processor_startup_get_kernel_cr3());
    serial_write_string(&com1, "\n");

    application_processor_startup_set_serial_port(&com1);

    if (!ap_bootstrap_ok)
        return;

    if (!advanced_pic_ipi_is_ready())
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: AP startup skipped (IPI not ready)\n");
        return;
    }

    if (!application_processor_startup_ensure_low_identity_mapping())
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: AP startup skipped (identity map unavailable)\n");
        return;
    }

    if (!application_processor_trampoline_install())
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: AP startup skipped (trampoline install failed)\n");
        return;
    }

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: AP trampoline installed, vector=");
    serial_write_hex8(&com1, application_processor_trampoline_get_startup_vector());
    serial_write_string(&com1, "\n");

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
            application_processor_trampoline_configure_handoff(entry->apic_id, entry->logical_slot,
                                                                (uint32_t) (uintptr_t) ap_stack_top,
                                                                (uint32_t) (uintptr_t) application_processor_startup_initialize_cpu,
                                                                (uint32_t) (uintptr_t) application_processor_startup_main_loop,
                                                                application_processor_startup_get_kernel_cr3());
            sequence_ok =
                advanced_pic_ipi_send_startup_sequence(entry->apic_id, application_processor_trampoline_get_startup_vector());
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

        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: AP startup dispatch apic_id=");
        serial_write_int(&com1, (int32_t) entry->apic_id);
        serial_write_string(&com1, ", slot=");
        serial_write_int(&com1, (int32_t) entry->logical_slot);
        serial_write_string(&com1, ", vector=");
        serial_write_hex8(&com1, application_processor_trampoline_get_startup_vector());
        serial_write_string(&com1, ", delivered=");
        serial_write_int(&com1, (int32_t) (sequence_ok && ack_ok));
        serial_write_string(&com1, ", seq_ok=");
        serial_write_int(&com1, (int32_t) sequence_ok);
        serial_write_string(&com1, ", ack_ok=");
        serial_write_int(&com1, (int32_t) ack_ok);
        serial_write_string(&com1, ", c_entry_ok=");
        serial_write_int(&com1, (int32_t) c_entry_ok);
        serial_write_string(&com1, ", ack_word=");
        serial_write_hex16(&com1, application_processor_trampoline_get_acknowledgement_word());
        serial_write_string(&com1, ", c_entry_word=");
        serial_write_hex16(&com1, application_processor_trampoline_get_c_entry_word());
        serial_write_string(&com1, ", attempts=");
        serial_write_int(&com1, (int32_t) attempts_used);
        serial_write_string(&com1, "\n");
    }

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: AP startup summary attempted=");
    serial_write_int(&com1, (int32_t) attempted);
    serial_write_string(&com1, ", delivered=");
    serial_write_int(&com1, (int32_t) delivered);
    serial_write_string(&com1, ", seq_attempts=");
    serial_write_int(&com1, (int32_t) advanced_pic_ipi_get_startup_sequence_attempt_count());
    serial_write_string(&com1, ", seq_success=");
    serial_write_int(&com1, (int32_t) advanced_pic_ipi_get_startup_sequence_success_count());
    serial_write_string(&com1, ", init_ipi=");
    serial_write_int(&com1, (int32_t) advanced_pic_ipi_get_init_attempt_count());
    serial_write_string(&com1, ", sipi=");
    serial_write_int(&com1, (int32_t) advanced_pic_ipi_get_sipi_attempt_count());
    serial_write_string(&com1, ", ap_reported_online=");
    serial_write_int(&com1, (int32_t) application_processor_startup_get_reported_online_count());
    serial_write_string(&com1, ", ap_last_id=");
    serial_write_int(&com1, (int32_t) application_processor_startup_get_last_reported_apic_id());
    serial_write_string(&com1, ", retries=");
    serial_write_int(&com1, (int32_t) retries_consumed);
    serial_write_string(&com1, ", seq_fail=");
    serial_write_int(&com1, (int32_t) sequence_failures);
    serial_write_string(&com1, ", ack_to=");
    serial_write_int(&com1, (int32_t) acknowledgement_timeouts);
    serial_write_string(&com1, ", c_entry_to=");
    serial_write_int(&com1, (int32_t) c_entry_timeouts);
    serial_write_string(&com1, ", tramp_installs=");
    serial_write_int(&com1, (int32_t) application_processor_trampoline_get_install_count());
    serial_write_string(&com1, ", tramp_waits=");
    serial_write_int(&com1, (int32_t) application_processor_trampoline_get_ack_wait_count());
    serial_write_string(&com1, ", tramp_ack_ok=");
    serial_write_int(&com1, (int32_t) application_processor_trampoline_get_ack_success_count());
    serial_write_string(&com1, ", tramp_ack_to=");
    serial_write_int(&com1, (int32_t) application_processor_trampoline_get_ack_timeout_count());
    serial_write_string(&com1, ", tramp_c_waits=");
    serial_write_int(&com1, (int32_t) application_processor_trampoline_get_c_entry_wait_count());
    serial_write_string(&com1, ", tramp_c_ok=");
    serial_write_int(&com1, (int32_t) application_processor_trampoline_get_c_entry_success_count());
    serial_write_string(&com1, ", tramp_c_to=");
    serial_write_int(&com1, (int32_t) application_processor_trampoline_get_c_entry_timeout_count());
    serial_write_string(&com1, "\n");
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
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: clock profile=");
    serial_write_string(&com1, clock_get_profile_name());
    serial_write_string(&com1, ", backend=");
    serial_write_string(&com1, clock_get_backend_name());
    serial_write_string(&com1, ", hz=");
    serial_write_int(&com1, (int32_t) clock_get_tick_hz());
    serial_write_string(&com1, ", rtc_periodic=");
    serial_write_int(&com1, (int32_t) clock_is_rtc_periodic_enabled());
    serial_write_string(&com1, "\n");

    cpu_topology_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: cpu topology slot=");
    serial_write_int(&com1, (int32_t) cpu_topology_get_logical_slot());
    serial_write_string(&com1, ", apic_id=");
    serial_write_int(&com1, (int32_t) cpu_topology_get_local_apic_id());
    serial_write_string(&com1, ", cpus=");
    serial_write_int(&com1, (int32_t) cpu_topology_get_discovered_cpu_count());
    serial_write_string(&com1, ", online=");
    serial_write_int(&com1, (int32_t) cpu_topology_get_online_cpu_count());
    serial_write_string(&com1, ", source=");
    serial_write_string(&com1, cpu_topology_get_source_name());
    serial_write_string(&com1, ", forced=");
    serial_write_int(&com1, (int32_t) cpu_topology_is_forced());
    serial_write_string(&com1, "\n");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing runtime paging...\n");
    paging_initialize_runtime();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: runtime paging initialized successfully!\n");
    kernel_vmm_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: kernel VMM initialized successfully!\n");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing PMM...\n");
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: PMM strategy=");
    serial_write_string(&com1, physical_memory_manager_get_strategy_name());
    serial_write_string(&com1, "\n");
    physical_memory_manager_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: PMM pass 1 (0-16MB) ready: ");
    serial_write_int(&com1, (int32_t) physical_memory_manager_get_free_page_count());
    serial_write_string(&com1, " pages free\n");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: PMM pass 2 (>16MB mapping)...\n");
    physical_memory_manager_extend_mapping();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: PMM ready \342\200\224 total free: ");
    serial_write_int(&com1, (int32_t) physical_memory_manager_get_free_page_count());
    serial_write_string(&com1, " pages (~");
    serial_write_int(&com1, (int32_t) (physical_memory_manager_get_free_page_count() * 4 / 1024));
    serial_write_string(&com1, " MB free), watermark_high=");
    serial_write_int(&com1, (int32_t) physical_memory_manager_get_watermark_high());
    serial_write_string(&com1, ", watermark_low=");
    serial_write_int(&com1, (int32_t) physical_memory_manager_get_watermark_low());
    serial_write_string(&com1, ", frag=");
    serial_write_int(&com1, (int32_t) physical_memory_manager_get_fragmentation_ratio());
    serial_write_string(&com1, "%\n");

    kernel_heap_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: heap strategy=");
    serial_write_string(&com1, kernel_heap_get_strategy_name());
#ifdef LPL_KERNEL_REAL_TIME_MODE
    serial_write_string(&com1, ", tlsf_pool=");
    serial_write_int(&com1, (int32_t) (kernel_tlsf_get_pool_size() / 1024));
    serial_write_string(&com1, "KB\n");
#else
    serial_write_string(&com1, "\n");
#endif
    serial_write_string(&com1, ", small_free_blocks=");
    serial_write_int(&com1, (int32_t) kernel_heap_get_small_free_block_count());
    serial_write_string(&com1, ", small_free_bytes=");
    serial_write_int(&com1, (int32_t) kernel_heap_get_small_free_bytes());
    serial_write_string(&com1, ", rejected_free=");
    serial_write_int(&com1, (int32_t) kernel_heap_debug_get_rejected_free_count());
    serial_write_string(&com1, ", double_free=");
    serial_write_int(&com1, (int32_t) kernel_heap_debug_get_double_free_count());
    serial_write_string(&com1, "\n");

    bool frame_arena_ok = kernel_frame_arena_initialize(KERNEL_FRAME_ARENA_DEFAULT_CAPACITY_BYTES);
    bool stack_allocator_ok = kernel_stack_allocator_initialize(KERNEL_STACK_ALLOCATOR_DEFAULT_CAPACITY_BYTES);
    bool pool_allocator_ok = kernel_pool_allocator_initialize(KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_SIZE,
                                                              KERNEL_POOL_ALLOCATOR_DEFAULT_OBJECT_COUNT);
    bool pinned_ok = kernel_pinned_memory_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: core allocators ready: arena=");
    serial_write_int(&com1, (int32_t) frame_arena_ok);
    serial_write_string(&com1, ", stack=");
    serial_write_int(&com1, (int32_t) stack_allocator_ok);
    serial_write_string(&com1, ", pool=");
    serial_write_int(&com1, (int32_t) pool_allocator_ok);
    serial_write_string(&com1, ", pinned=");
    serial_write_int(&com1, (int32_t) pinned_ok);
    serial_write_string(&com1, ", arena_capacity=");
    serial_write_int(&com1, (int32_t) kernel_frame_arena_get_capacity_bytes());
    serial_write_string(&com1, ", arena_budget_exceeded=");
    serial_write_int(&com1, (int32_t) kernel_frame_arena_get_budget_exceeded_count());
    serial_write_string(&com1, ", stack_capacity=");
    serial_write_int(&com1, (int32_t) kernel_stack_allocator_get_capacity());
    serial_write_string(&com1, ", pool_object_size=");
    serial_write_int(&com1, (int32_t) kernel_pool_get_object_size());
    serial_write_string(&com1, ", pool_capacity=");
    serial_write_int(&com1, (int32_t) kernel_pool_get_capacity());
    serial_write_string(&com1, ", free=");
    serial_write_int(&com1, (int32_t) kernel_pool_get_free_count());
    serial_write_string(&com1, "\n");

    bool ring_buffer_ok = kernel_ring_buffer_initialize_ex(KERNEL_RING_BUFFER_DEFAULT_SLOT_SIZE,
                                                           KERNEL_RING_BUFFER_DEFAULT_SLOT_COUNT,
                                                           KERNEL_RING_BUFFER_MODE_SPSC);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: ring buffer init=");
    serial_write_int(&com1, (int32_t) ring_buffer_ok);
    serial_write_string(&com1, ", mode=");
    serial_write_int(&com1, (int32_t) kernel_ring_buffer_get_mode());
    serial_write_string(&com1, ", slot_size=");
    serial_write_int(&com1, (int32_t) kernel_ring_buffer_get_slot_size());
    serial_write_string(&com1, ", capacity=");
    serial_write_int(&com1, (int32_t) kernel_ring_buffer_get_capacity());
    serial_write_string(&com1, ", count=");
    serial_write_int(&com1, (int32_t) kernel_ring_buffer_get_count());
    serial_write_string(&com1, ", enq=");
    serial_write_int(&com1, (int32_t) kernel_ring_buffer_get_enqueue_count());
    serial_write_string(&com1, ", deq=");
    serial_write_int(&com1, (int32_t) kernel_ring_buffer_get_dequeue_count());
    serial_write_string(&com1, "\n");

#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: slab free: sz16=");
    serial_write_int(&com1, (int32_t) kernel_slab_get_free_count(16u));
    serial_write_string(&com1, ", sz64=");
    serial_write_int(&com1, (int32_t) kernel_slab_get_free_count(64u));
    serial_write_string(&com1, ", sz256=");
    serial_write_int(&com1, (int32_t) kernel_slab_get_free_count(256u));
    serial_write_string(&com1, ", hot_loop_depth=");
    serial_write_int(&com1, (int32_t) kernel_heap_get_hot_loop_depth());
    serial_write_string(&com1, ", hot_loop_violations=");
    serial_write_int(&com1, (int32_t) kernel_heap_get_hot_loop_violation_count());
    serial_write_string(&com1, "\n");
#else
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: server heap domains=");
    serial_write_int(&com1, (int32_t) kernel_heap_get_server_domain_count());
    serial_write_string(&com1, ", active=");
    serial_write_int(&com1, (int32_t) kernel_heap_get_server_active_domain());
    serial_write_string(&com1, ", first_fit_fallbacks=");
    serial_write_int(&com1,
                     (int32_t) kernel_heap_get_server_domain_first_fit_fallback_count(
                         kernel_heap_get_server_active_domain()));
    serial_write_string(&com1, ", remote_probe=");
    serial_write_int(&com1,
                     (int32_t) kernel_heap_get_server_domain_remote_probe_count(
                         kernel_heap_get_server_active_domain()));
    serial_write_string(&com1, ", remote_hit=");
    serial_write_int(&com1,
                     (int32_t) kernel_heap_get_server_domain_remote_hit_count(
                         kernel_heap_get_server_active_domain()));
    serial_write_string(&com1, "\n");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: sizeclass buckets free/hits: ");
    for (uint32_t sc = 0u; sc < 7u; ++sc)
    {
        serial_write_string(&com1, "s");
        serial_write_int(&com1, (int32_t) sc);
        serial_write_string(&com1, "=");
        serial_write_int(&com1, (int32_t) kernel_heap_get_size_class_free_count(sc));
        serial_write_string(&com1, "/");
        serial_write_int(&com1, (int32_t) kernel_heap_get_size_class_hit_count(sc));
        serial_write_string(&com1, "/");
        serial_write_int(&com1,
                         (int32_t) kernel_heap_get_server_domain_refill_count(
                             kernel_heap_get_server_active_domain(), sc));
        serial_write_string(&com1, " ");
    }
    serial_write_string(&com1, "\n");
#endif

#if !defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: PMM buddy free blocks by order: ");
    for (uint8_t order = 0u; order <= 18u; ++order)
    {
        uint32_t count = physical_memory_manager_debug_get_free_block_count(order);

        if (!count)
            continue;

        serial_write_string(&com1, "o");
        serial_write_int(&com1, (int32_t) order);
        serial_write_string(&com1, "=");
        serial_write_int(&com1, (int32_t) count);
        serial_write_string(&com1, " ");
    }
    serial_write_string(&com1, "\n");
#endif

    advanced_configuration_and_power_interface_madt_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: ACPI MADT state=");
    serial_write_string(&com1, advanced_configuration_and_power_interface_madt_get_state_name());
    serial_write_string(&com1, ", madt=");
    serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_is_available());
    serial_write_string(&com1, ", lapic_phys=");
    serial_write_hex32(&com1, advanced_configuration_and_power_interface_madt_get_local_apic_physical_base());
    serial_write_string(&com1, ", ioapic_count=");
    serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_count());
    serial_write_string(&com1, ", lapic_count=");
    serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_local_apic_count());
    serial_write_string(&com1, ", iso_count=");
    serial_write_int(&com1,
                     (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count());
    serial_write_string(&com1, "\n");

    if (advanced_configuration_and_power_interface_madt_is_available())
    {
        for (uint8_t lapic_index = 0u;
             lapic_index < advanced_configuration_and_power_interface_madt_get_local_apic_count();
             ++lapic_index)
        {
            if (!advanced_configuration_and_power_interface_madt_is_local_apic_enabled(lapic_index))
                continue;

            (void) cpu_topology_register_discovered_apic_id(
                advanced_configuration_and_power_interface_madt_get_local_apic_id(lapic_index));
        }

        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: cpu topology MADT sync cpus=");
        serial_write_int(&com1, (int32_t) cpu_topology_get_discovered_cpu_count());
        serial_write_string(&com1, "\n");
    }

    for (uint8_t ioapic_index = 0u; ioapic_index < advanced_configuration_and_power_interface_madt_get_io_apic_count();
         ++ioapic_index)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: ACPI IOAPIC[");
        serial_write_int(&com1, (int32_t) ioapic_index);
        serial_write_string(&com1, "] id=");
        serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_id(ioapic_index));
        serial_write_string(&com1, ", phys=");
        serial_write_hex32(&com1,
                           advanced_configuration_and_power_interface_madt_get_io_apic_physical_base(ioapic_index));
        serial_write_string(&com1, ", gsi_base=");
        serial_write_int(&com1,
                         (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_gsi_base(ioapic_index));
        serial_write_string(&com1, "\n");
    }

    for (uint8_t iso_index = 0u;
         iso_index < advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count(); ++iso_index)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: ACPI ISO[");
        serial_write_int(&com1, (int32_t) iso_index);
        serial_write_string(&com1, "] bus=");
        serial_write_int(
            &com1,
            (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_bus(iso_index));
        serial_write_string(&com1, ", source_irq=");
        serial_write_int(
            &com1, (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_source_irq(
                       iso_index));
        serial_write_string(&com1, ", gsi=");
        serial_write_int(
            &com1,
            (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_gsi(iso_index));
        serial_write_string(&com1, ", flags=");
        serial_write_hex16(
            &com1, advanced_configuration_and_power_interface_madt_get_interrupt_source_override_flags(iso_index));
        serial_write_string(&com1, "\n");
    }

    for (uint8_t isa_irq = 0u; isa_irq <= 1u; ++isa_irq)
    {
        uint32_t mapped_gsi = 0u;
        uint16_t mapped_flags = 0u;
        uint8_t ioapic_index = 0u;

        if (!advanced_configuration_and_power_interface_madt_resolve_isa_irq(isa_irq, &mapped_gsi, &mapped_flags))
            continue;

        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: ACPI route ISA IRQ");
        serial_write_int(&com1, (int32_t) isa_irq);
        serial_write_string(&com1, " -> GSI ");
        serial_write_int(&com1, (int32_t) mapped_gsi);
        serial_write_string(&com1, ", flags=");
        serial_write_hex16(&com1, mapped_flags);

        if (advanced_configuration_and_power_interface_madt_find_io_apic_for_gsi(mapped_gsi, &ioapic_index))
        {
            serial_write_string(&com1, ", ioapic_index=");
            serial_write_int(&com1, (int32_t) ioapic_index);
            serial_write_string(&com1, ", ioapic_id=");
            serial_write_int(&com1,
                             (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_id(ioapic_index));
        }
        else
            serial_write_string(&com1, ", ioapic_index=none");

        serial_write_string(&com1, "\n");
    }

    input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: IOAPIC scaffold state=");
    serial_write_string(&com1, input_output_advanced_programmable_interrupt_controller_get_state_name());
    serial_write_string(&com1, ", mapped=");
    serial_write_int(&com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_mapped_count());
    serial_write_string(&com1, ", routes=");
    serial_write_int(&com1,
                     (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_count());
    serial_write_string(&com1, "\n");

    for (uint8_t route_index = 0u;
         route_index < input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();
         ++route_index)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: IOAPIC route[");
        serial_write_int(&com1, (int32_t) route_index);
        serial_write_string(&com1, "] isa_irq=");
        serial_write_int(
            &com1,
            (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_irq(route_index));
        serial_write_string(&com1, ", gsi=");
        serial_write_int(
            &com1,
            (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_gsi(route_index));
        serial_write_string(&com1, ", vector=");
        serial_write_int(
            &com1,
            (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_vector(route_index));
        serial_write_string(&com1, ", ioapic_index=");
        serial_write_int(
            &com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_index(
                       route_index));
        serial_write_string(&com1, ", ioapic_id=");
        serial_write_int(
            &com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_id(
                       route_index));
        serial_write_string(&com1, ", masked=");
        serial_write_int(
            &com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_is_masked(
                       route_index));
        serial_write_string(&com1, ", iso_flags=");
        serial_write_hex16(
            &com1, input_output_advanced_programmable_interrupt_controller_get_programmed_route_iso_flags(route_index));
        serial_write_string(&com1, "\n");
    }

#if defined(LPL_KERNEL_APIC_TIMER_BACKEND)
    if (advanced_pic_timer_backend_late_initialize())
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC late init state=");
        serial_write_string(&com1, advanced_pic_timer_backend_name());
        serial_write_string(&com1, ", lapic_phys=");
        serial_write_hex32(&com1, advanced_pic_timer_backend_get_local_apic_physical_base());
        serial_write_string(&com1, ", mmio_mapped=");
        serial_write_int(&com1, (int32_t) advanced_pic_timer_backend_is_local_apic_mmio_mapped());
        serial_write_string(&com1, ", lapic_ver=");
        serial_write_hex32(&com1, advanced_pic_timer_backend_get_local_apic_version_register());
        serial_write_string(&com1, "\n");

        /* Initialize APIC IPI framework for AP startup */
        advanced_pic_ipi_initialize(advanced_pic_timer_backend_get_local_apic_virtual_base());
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC IPI framework initialized\n");

        /* Start discovered APs only after MADT sync + APIC IPI readiness */
        kernel_try_start_discovered_aps();

        if (kernel_policy_enable_ioapic_keyboard_owner())
        {
            if (input_output_advanced_programmable_interrupt_controller_enable_isa_route(1u))
            {
                programmable_interrupt_controller_set_mask(1u);
                interrupt_request_set_keyboard_owner_is_apic(1u);

                serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: IOAPIC keyboard handoff state=");
                serial_write_string(&com1, input_output_advanced_programmable_interrupt_controller_get_state_name());
                serial_write_string(&com1, ", irq1_owner_apic=");
                serial_write_int(&com1, (int32_t) interrupt_request_is_keyboard_owner_apic());
                serial_write_string(&com1, "\n");
            }
            else
            {
                serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: IOAPIC keyboard handoff fallback state=");
                serial_write_string(&com1, input_output_advanced_programmable_interrupt_controller_get_state_name());
                serial_write_string(&com1, ", irq1_owner_apic=");
                serial_write_int(&com1, (int32_t) interrupt_request_is_keyboard_owner_apic());
                serial_write_string(&com1, "\n");
            }
        }
        else
        {
            serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: IOAPIC keyboard ownership policy=pic-fallback\n");
        }

        if (advanced_pic_timer_backend_calibrate_with_pit())
        {
            serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC calibration state=");
            serial_write_string(&com1, advanced_pic_timer_backend_name());
            serial_write_string(&com1, ", lapic_timer_hz_estimate=");
            serial_write_int(&com1, (int32_t) advanced_pic_timer_backend_get_calibrated_timer_frequency_hz());
            serial_write_string(&com1, "\n");

            if (kernel_policy_enable_apic_timer_owner())
            {
                if (advanced_pic_timer_backend_enable_periodic_mode(interrupt_request_get_timer_frequency_hz()))
                {
                    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC owner handoff state=");
                    serial_write_string(&com1, advanced_pic_timer_backend_name());
                    serial_write_string(&com1, ", apic_owner=");
                    serial_write_int(&com1, (int32_t) interrupt_request_is_timer_owner_apic());
                    serial_write_string(&com1, ", apic_periodic=");
                    serial_write_int(&com1, (int32_t) advanced_pic_timer_backend_is_periodic_mode_enabled());
                    serial_write_string(&com1, "\n");
                }
                else
                {
                    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC owner handoff fallback state=");
                    serial_write_string(&com1, advanced_pic_timer_backend_name());
                    serial_write_string(&com1, ", apic_owner=");
                    serial_write_int(&com1, (int32_t) interrupt_request_is_timer_owner_apic());
                    serial_write_string(&com1, ", apic_periodic=");
                    serial_write_int(&com1, (int32_t) advanced_pic_timer_backend_is_periodic_mode_enabled());
                    serial_write_string(&com1, "\n");
                }
            }
            else
            {
                serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC ownership policy=pit-fallback\n");
            }
        }
        else
        {
            serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC calibration skipped state=");
            serial_write_string(&com1, advanced_pic_timer_backend_name());
            serial_write_string(&com1, "\n");
        }
    }
    else
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC late init skipped state=");
        serial_write_string(&com1, advanced_pic_timer_backend_name());
        serial_write_string(&com1, "\n");
    }
#endif

    if (KERNEL_SMOKE_TEST_ENABLE_PMM_ALLOCATE_FREE)
        kernel_smoke_test_run_physical_memory_manager_allocate_free(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_PAGING_PT_RECLAIM)
        kernel_smoke_test_run_paging_runtime_page_table_reclaim(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_HEAP_ALLOCATE_FREE)
        kernel_smoke_test_run_heap_allocate_free(&com1);

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

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: PMM guard counters: rejected_free=");
    serial_write_int(&com1, (int32_t) physical_memory_manager_debug_get_rejected_free_count());
    serial_write_string(&com1, ", double_free=");
    serial_write_int(&com1, (int32_t) physical_memory_manager_debug_get_double_free_count());
    serial_write_string(&com1, "\n");

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

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: keyboard runtime: irq=");
    serial_write_int(&com1, (int32_t) keyboard_get_irq_count());
    serial_write_string(&com1, ", printable=");
    serial_write_int(&com1, (int32_t) keyboard_get_printable_count());
    serial_write_string(&com1, ", pending=");
    serial_write_int(&com1, (int32_t) keyboard_get_pending_char_count());
    serial_write_string(&com1, ", dropped=");
    serial_write_int(&com1, (int32_t) keyboard_get_dropped_char_count());
    serial_write_string(&com1, ", last='");
    serial_write_int(&com1, (int32_t) keyboard_get_last_printable_char());
    serial_write_string(&com1, "'\n");

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
