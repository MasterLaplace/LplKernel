#include <kernel/config.h>
#include <kernel/core/splash.h>
#include <kernel/drivers/tty.h>
#include <kernel/lib/asmutils.h>

static uint32_t g_splash_total_steps = 0u;
static uint32_t g_splash_current_step = 0u;
static const uint32_t SPLASH_BAR_WIDTH_CHARS = 40u;

static void kernel_splash_draw(const char *step_name)
{
    uint32_t max_name_len = 50u;
    uint32_t name_len = 0u;

    terminal_reset_pos();

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_write_string("\n\n  [" KERNEL_SYSTEM_STRING "] Bootstrap Sequence\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_write_string("  ");

    if (step_name)
    {
        for (; step_name[name_len] && name_len < max_name_len; ++name_len)
        {
            terminal_putchar(step_name[name_len]);
        }
    }

    for (uint32_t i = name_len; i < max_name_len; ++i)
        terminal_putchar(' ');

    terminal_write_string("\n\n  [");

    uint32_t filled = 0u;
    if (g_splash_total_steps > 0u)
        filled = (g_splash_current_step * SPLASH_BAR_WIDTH_CHARS) / g_splash_total_steps;

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    for (uint32_t i = 0u; i < SPLASH_BAR_WIDTH_CHARS; ++i)
    {
        if (i < filled)
            terminal_putchar('=');
        else if (i == filled)
            terminal_putchar('>');
        else
            terminal_putchar(' ');
    }
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_write_string("] ");

    uint32_t pct = 0u;
    if (g_splash_total_steps > 0u)
        pct = (g_splash_current_step * 100u) / g_splash_total_steps;

    if (pct < 10u)
        terminal_putchar(' ');
    if (pct < 100u)
        terminal_putchar(' ');
    terminal_write_number(pct, 10u);
    terminal_write_string("%\n");
}

void kernel_splash_initialize(uint32_t total_steps)
{
    g_splash_total_steps = total_steps;
    g_splash_current_step = 0u;
    terminal_clear();
    kernel_splash_draw("Initializing hardware...");
}

void kernel_splash_update(const char *step_name)
{
    if (g_splash_current_step < g_splash_total_steps)
        g_splash_current_step++;

    kernel_splash_draw(step_name);
}

void kernel_splash_finish(void)
{
    terminal_clear();
    terminal_reset_pos();
}
