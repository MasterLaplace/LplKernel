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

    framebuffer_initialize();

    // Dessiner un rectangle rouge pour tester
    for (uint32_t y = 100; y < 200; ++y) {
        for (uint32_t x = 100; x < 300; ++x) {
            framebuffer_set_pixel(x, y, 0xFF0000); // Rouge
        }
    }

    terminal_write_string("Framebuffer test complete.\n");

    for (;;);
}
