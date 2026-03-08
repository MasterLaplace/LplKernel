#define __LPL_KERNEL__

#include <kernel/config.h>

#include <kernel/cpu/clock.h>
#include <kernel/cpu/pmm.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/smoke_test.h>

void kernel_smoke_test_run_physical_memory_manager_allocate_free(Serial_t *serial_port)
{
    uint32_t page_address_1 = physical_memory_manager_page_frame_allocate();
    uint32_t page_address_2 = physical_memory_manager_page_frame_allocate();

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: example allocate -> ");
    serial_write_hex32(serial_port, page_address_1);
    serial_write_string(serial_port, ", ");
    serial_write_hex32(serial_port, page_address_2);
    serial_write_string(serial_port, "\n");

    if (page_address_1)
        physical_memory_manager_page_frame_free(page_address_1);
    if (page_address_2)
        physical_memory_manager_page_frame_free(page_address_2);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: after free count = ");
    serial_write_int(serial_port, (int32_t) physical_memory_manager_get_free_page_count());
    serial_write_string(serial_port, "\n");
}

void kernel_smoke_test_run_division_error(void)
{
    volatile int zero = 0;
    volatile int trap = 1 / zero;

    (void) trap;
}

void kernel_smoke_test_run_debug_exception(void)
{
    __asm__ volatile("pushf\n\t"
                     "orl $0x100, (%%esp)\n\t"
                     "popf\n\t"
                     "nop\n\t"
                     :
                     :
                     : "cc", "memory");
}

void kernel_smoke_test_run_breakpoint_exception(void)
{
    __asm__ volatile("int3");
}

void kernel_smoke_test_run_invalid_opcode_exception(void)
{
    __asm__ volatile("ud2");
}

void kernel_smoke_test_run_graphics_demo(Serial_t *serial_port)
{
    framebuffer_clear(framebuffer_rgb(0, 0, 64));
    framebuffer_fill_rect(50, 50, 200, 100, COLOR_RED);
    framebuffer_draw_rect(300, 50, 200, 100, COLOR_GREEN);
    framebuffer_fill_rect(550, 50, 200, 100, COLOR_BLUE);

    for (uint32_t x = 50; x < 750; x++)
    {
        uint8_t red = (x - 50u) * 255u / 700u;
        uint8_t green = 255u - red;

        framebuffer_draw_vline(x, 200, 280, framebuffer_rgb(red, green, 128));
    }

    framebuffer_draw_hline(50, 750, 320, COLOR_WHITE);
    framebuffer_draw_hline(50, 750, 340, COLOR_YELLOW);
    framebuffer_draw_hline(50, 750, 360, COLOR_CYAN);
    framebuffer_draw_hline(50, 750, 380, COLOR_MAGENTA);

    for (uint32_t y = 420; y < 620; y += 2)
    {
        for (uint32_t x = 50; x < 250; x += 2)
            framebuffer_put_pixel(x, y, COLOR_WHITE);
    }

    for (uint32_t i = 0; i < 50; i += 5)
    {
        color_t color = framebuffer_rgb(i * 5u, 255u - i * 5u, 128u);
        framebuffer_draw_rect(300 + i, 420 + i, 200 - 2 * i, 200 - 2 * i, color);
    }

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: graphics demo displayed!\n");
}

void kernel_smoke_test_run_interrupt_request_runtime_status(Serial_t *serial_port)
{
    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: IRQ runtime status: ticks=");
    serial_write_int(serial_port, (int32_t) clock_get_tick_count());
    serial_write_string(serial_port, ", pit_hz=");
    serial_write_int(serial_port, (int32_t) clock_get_tick_hz());
    serial_write_string(serial_port, ", spurious7=");
    serial_write_int(serial_port, (int32_t) clock_get_spurious_irq7_count());
    serial_write_string(serial_port, ", spurious15=");
    serial_write_int(serial_port, (int32_t) clock_get_spurious_irq15_count());
    serial_write_string(serial_port, ", rtc_irq=");
    serial_write_int(serial_port, (int32_t) clock_get_rtc_periodic_interrupt_count());
    serial_write_string(serial_port, ", rtc_periodic=");
    serial_write_int(serial_port, (int32_t) clock_is_rtc_periodic_enabled());
    serial_write_string(serial_port, "\n");
}

void kernel_smoke_test_run_realtime_clock_snapshot(Serial_t *serial_port)
{
    RealtimeClockTime_t current_time;

    clock_read_walltime(&current_time);

    serial_write_string(serial_port, "[" KERNEL_SYSTEM_STRING "]: RTC snapshot: ");
    serial_write_int(serial_port, (int32_t) current_time.hour);
    serial_write_string(serial_port, ":");
    serial_write_int(serial_port, (int32_t) current_time.minute);
    serial_write_string(serial_port, ":");
    serial_write_int(serial_port, (int32_t) current_time.second);
    serial_write_string(serial_port, " ");
    serial_write_int(serial_port, (int32_t) current_time.day);
    serial_write_string(serial_port, "/");
    serial_write_int(serial_port, (int32_t) current_time.month);
    serial_write_string(serial_port, "/");
    serial_write_int(serial_port, (int32_t) current_time.year);
    serial_write_string(serial_port, "\n");
}
