#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <kernel/boot/multiboot_info_helper.h>
#include <kernel/cpu/gdt_helper.h>
#include <kernel/cpu/paging.h>
#include <kernel/drivers/framebuffer.h>

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

    // print_multiboot_info(KERNEL_VIRTUAL_BASE, multiboot_info);
    write_multiboot_info(&com1, KERNEL_VIRTUAL_BASE, multiboot_info);

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing GDT...\n");
    global_descriptor_table_initialize(&global_descriptor_table, &task_state_segment);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: loading GDT into CPU...\n");
    global_descriptor_table_load(&global_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: GDT loaded successfully!\n");
    write_global_descriptor_table(&com1, &global_descriptor_table);

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing runtime paging...\n");
    paging_initialize_runtime();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: runtime paging initialized successfully!\n");

    if (framebuffer_init())
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: framebuffer initialized successfully!\n");
    else
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: no linear framebuffer available (text mode)\n");
}

void kernel_main(void)
{
    if (framebuffer_available())
    {
        /* Graphics mode demo */
        const framebuffer_info_t *fb = framebuffer_get_info();

        /* Clear screen to dark blue */
        framebuffer_clear(framebuffer_rgb(0, 0, 64));

        /* Draw some shapes */
        /* Red rectangle at top-left */
        framebuffer_fill_rect(50, 50, 200, 100, COLOR_RED);

        /* Green rectangle outline */
        framebuffer_draw_rect(300, 50, 200, 100, COLOR_GREEN);

        /* Blue filled rectangle */
        framebuffer_fill_rect(550, 50, 200, 100, COLOR_BLUE);

        /* Draw a gradient bar */
        for (uint32_t x = 50; x < 750; x++)
        {
            uint8_t r = (x - 50) * 255 / 700;
            uint8_t g = 255 - r;
            uint8_t b = 128;
            framebuffer_draw_vline(x, 200, 280, framebuffer_rgb(r, g, b));
        }

        /* Draw horizontal lines */
        framebuffer_draw_hline(50, 750, 320, COLOR_WHITE);
        framebuffer_draw_hline(50, 750, 340, COLOR_YELLOW);
        framebuffer_draw_hline(50, 750, 360, COLOR_CYAN);
        framebuffer_draw_hline(50, 750, 380, COLOR_MAGENTA);

        /* Draw a simple pattern */
        for (uint32_t y = 420; y < 620; y += 2)
        {
            for (uint32_t x = 50; x < 250; x += 2)
                framebuffer_put_pixel(x, y, COLOR_WHITE);
        }

        /* Draw concentric rectangles */
        for (uint32_t i = 0; i < 50; i += 5)
        {
            color_t col = framebuffer_rgb(i * 5, 255 - i * 5, 128);
            framebuffer_draw_rect(300 + i, 420 + i, 200 - 2 * i, 200 - 2 * i, col);
        }

        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: graphics demo displayed!\n");
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
