#define __LPL_KERNEL__

#include <kernel/config.h>

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
