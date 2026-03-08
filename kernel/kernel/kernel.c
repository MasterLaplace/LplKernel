#define __LPL_KERNEL__
#include <kernel/config.h>

#include <kernel/boot/multiboot_info_helper.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/gdt_helper.h>
#include <kernel/cpu/idt.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/paging.h>
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

#if defined(LPL_KERNEL_EXPERIMENTAL_APIC_TIMER_BACKEND)
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

        if (advanced_pic_timer_backend_calibrate_with_pit())
        {
            serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: APIC calibration state=");
            serial_write_string(&com1, advanced_pic_timer_backend_name());
            serial_write_string(&com1, ", lapic_timer_hz_estimate=");
            serial_write_int(&com1, (int32_t) advanced_pic_timer_backend_get_calibrated_timer_frequency_hz());
            serial_write_string(&com1, "\n");
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
