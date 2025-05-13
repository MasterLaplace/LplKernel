#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <stdio.h>
#include <kernel/tty.h>
#include <kernel/framebuffer.h>

const char WELCOME_MESSAGE[] = ""
"/==+--  _                                         ---+\n"
"| \\|   | |                  _                        |\n"
"+  |   | |     __ _  ____  | |    __ _  ____  ___    |\n"
"   |   | |    / _` ||  _ \\ | |   / _` |/ ___\\/ _ \\   |\n"
"   |   | |___| (_| || |_| || |__| (_| | (___|  __/   |\\\n"
"   |   \\_____|\\__,_|| ___/ \\____|\\__,_|\\____/\\___|   ||\n"
"   |                | |_                             | \\\n"
"   +---             |___|                          --+==+\n\n";

void kernel_main(void)
{
    terminal_initialize();

    terminal_write_string(WELCOME_MESSAGE);

    terminal_write_string("Laplace kernel loading ...\n\n");

    terminal_write_string(KERNEL_CONFIG_STRING);
    // terminal_write_string("Laplace kernel loaded\n");

    // framebuffer_initialize();
    // framebuffer_set_pixel(100, 100, 0xFF0000);

    // uint16_t width = video_get_width();
    // uint16_t height = video_get_height();
    // uint8_t depth = video_get_depth();
    // uint16_t pitch = video_get_pitch();

    // terminal_write_number((long)width, 10);
    // terminal_write_string("x");
    // terminal_write_number((long)height, 10);
    // terminal_write_string("x");
    // terminal_write_number((long)depth, 10);
    // terminal_write_string("x");
    // terminal_write_number((long)pitch, 10);
    // terminal_write_string("\n");

    // // printf("Hello, kernel World!\n");

    // video_draw_rect(0, 0, 100, 100, 0xFFFFFF);


    for (;;);
}
