#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/boot/multiboot_info_helper.h>
#include <kernel/cpu/acpi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/gdt_helper.h>
#include <kernel/cpu/idt.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pic.h>
#include <kernel/cpu/pmm.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/smoke_test.h>

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

static GlobalDescriptorTable_t global_descriptor_table = {0};
static InterruptDescriptorTable_t interrupt_descriptor_table = {0};
static TaskStateSegment_t task_state_segment = {0};
static Serial_t com1;

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

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing runtime paging...\n");
    paging_initialize_runtime();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: runtime paging initialized successfully!\n");

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing PMM...\n");
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
    serial_write_string(&com1, " MB free)\n");

    advanced_configuration_and_power_interface_madt_initialize();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: ACPI MADT state=");
    serial_write_string(&com1, advanced_configuration_and_power_interface_madt_get_state_name());
    serial_write_string(&com1, ", madt=");
    serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_is_available());
    serial_write_string(&com1, ", lapic_phys=");
    serial_write_hex32(&com1, advanced_configuration_and_power_interface_madt_get_local_apic_physical_base());
    serial_write_string(&com1, ", ioapic_count=");
    serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_count());
    serial_write_string(&com1, ", iso_count=");
    serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count());
    serial_write_string(&com1, "\n");

    for (uint8_t ioapic_index = 0u; ioapic_index < advanced_configuration_and_power_interface_madt_get_io_apic_count(); ++ioapic_index)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: ACPI IOAPIC[");
        serial_write_int(&com1, (int32_t) ioapic_index);
        serial_write_string(&com1, "] id=");
        serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_id(ioapic_index));
        serial_write_string(&com1, ", phys=");
        serial_write_hex32(&com1, advanced_configuration_and_power_interface_madt_get_io_apic_physical_base(ioapic_index));
        serial_write_string(&com1, ", gsi_base=");
        serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_gsi_base(ioapic_index));
        serial_write_string(&com1, "\n");
    }

    for (uint8_t iso_index = 0u; iso_index < advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count(); ++iso_index)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: ACPI ISO[");
        serial_write_int(&com1, (int32_t) iso_index);
        serial_write_string(&com1, "] bus=");
        serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_bus(iso_index));
        serial_write_string(&com1, ", source_irq=");
        serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_source_irq(iso_index));
        serial_write_string(&com1, ", gsi=");
        serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_gsi(iso_index));
        serial_write_string(&com1, ", flags=");
        serial_write_hex16(&com1, advanced_configuration_and_power_interface_madt_get_interrupt_source_override_flags(iso_index));
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
            serial_write_int(&com1, (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_id(ioapic_index));
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
    serial_write_int(&com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_count());
    serial_write_string(&com1, "\n");

    for (uint8_t route_index = 0u; route_index < input_output_advanced_programmable_interrupt_controller_get_programmed_route_count(); ++route_index)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: IOAPIC route[");
        serial_write_int(&com1, (int32_t) route_index);
        serial_write_string(&com1, "] isa_irq=");
        serial_write_int(&com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_irq(route_index));
        serial_write_string(&com1, ", gsi=");
        serial_write_int(&com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_gsi(route_index));
        serial_write_string(&com1, ", vector=");
        serial_write_int(&com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_vector(route_index));
        serial_write_string(&com1, ", ioapic_index=");
        serial_write_int(&com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_index(route_index));
        serial_write_string(&com1, ", ioapic_id=");
        serial_write_int(&com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_id(route_index));
        serial_write_string(&com1, ", masked=");
        serial_write_int(&com1, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_is_masked(route_index));
        serial_write_string(&com1, ", iso_flags=");
        serial_write_hex16(&com1, input_output_advanced_programmable_interrupt_controller_get_programmed_route_iso_flags(route_index));
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

#if defined(LPL_KERNEL_IOAPIC_KEYBOARD_OWNER)
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
#endif

        if (advanced_pic_timer_backend_calibrate_with_pit())
        {
            serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC calibration state=");
            serial_write_string(&com1, advanced_pic_timer_backend_name());
            serial_write_string(&com1, ", lapic_timer_hz_estimate=");
            serial_write_int(&com1, (int32_t) advanced_pic_timer_backend_get_calibrated_timer_frequency_hz());
            serial_write_string(&com1, "\n");

#if defined(LPL_KERNEL_APIC_TIMER_OWNER)
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
#endif
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

    if (KERNEL_SMOKE_TEST_ENABLE_IRQ_RUNTIME_STATUS)
        kernel_smoke_test_run_interrupt_request_runtime_status(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_RTC_SNAPSHOT)
        kernel_smoke_test_run_realtime_clock_snapshot(&com1);

    if (KERNEL_SMOKE_TEST_ENABLE_APIC_PERIODIC_MODE)
        kernel_smoke_test_run_apic_periodic_mode(&com1);

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
    for (uint8_t c = 0u; c != KEY_ECHAP;)
    {
        c = serial_read_char(&com1);
        if (!framebuffer_available())
            terminal_putchar(c);
    }
}

__attribute__((destructor)) void kernel_cleanup(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write_string("\n[" KERNEL_SYSTEM_STRING "]: exiting ... panic!\n");
}
