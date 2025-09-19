#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>

////////////////////////////////////////////////////////////
// Private members of the terminal module
////////////////////////////////////////////////////////////

static const uint16_t VGA_WIDTH = 80u;
static const uint16_t VGA_HEIGHT = 25u;

static uint16_t terminal_row = 0u;
static uint16_t terminal_column = 0u;
static uint8_t terminal_color = 0x0Fu;
static volatile uint16_t *const terminal_buffer = (uint16_t *) 0xC03FF000;

////////////////////////////////////////////////////////////
// Private functions of the terminal module
////////////////////////////////////////////////////////////

static inline void terminal_putentryat(uint8_t c, uint8_t color, uint16_t row, uint16_t col)
{
    const uint16_t index = row * VGA_WIDTH + col;
    terminal_buffer[index] = vga_entry(c, color);
}

static inline void terminal_reset_pos(void)
{
    terminal_row = 0u;
    terminal_column = 0u;
}

static inline void terminal_clear(void)
{
    for (uint16_t row = 0; row < VGA_HEIGHT; ++row)
    {
        for (uint16_t col = 0; col < VGA_WIDTH; ++col)
        {
            const uint16_t index = row * VGA_WIDTH + col;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

static inline void terminal_scroll(void)
{
    for (uint16_t row = 1; row < VGA_HEIGHT; ++row)
    {
        for (uint16_t col = 0; col < VGA_WIDTH; ++col)
        {
            uint16_t src_index = row * VGA_WIDTH + col;
            uint16_t dst_index = (row - 1) * VGA_WIDTH + col;
            terminal_buffer[dst_index] = terminal_buffer[src_index];
        }
    }
}

static inline void terminal_delete_last_line(void)
{
    for (uint16_t col = 0u; col < VGA_WIDTH; ++col)
    {
        uint16_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + col;
        terminal_buffer[index] = vga_entry(' ', terminal_color);
    }
}

////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

void terminal_initialize(void)
{
    for (uint16_t row = 0u; row < VGA_HEIGHT; ++row)
    {
        for (uint16_t col = 0u; col < VGA_WIDTH; ++col)
        {
            const uint16_t index = row * VGA_WIDTH + col;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

void terminal_setcolor(uint8_t color) { terminal_color = color; }

void terminal_putchar(char c)
{
    if (!isprint(c) && !isspace(c))
        return;

    switch (c)
    {
    case '\r':
    case '\n': terminal_column = 0u;
    /* fallthrough */
    case '\v': ++terminal_row; break;
    case '\t':
        terminal_column = (terminal_column + 4u) & ~3u;
        break;
    case 127:
        if (terminal_column == 0u)
            return;

        --terminal_column;
        terminal_putentryat(' ', terminal_color, terminal_row, terminal_column);
        break;
    default:
        terminal_putentryat(c, terminal_color, terminal_row, terminal_column);
        ++terminal_column;
        break;
    }

    if (terminal_column >= VGA_WIDTH)
    {
        terminal_column = 0u;
        ++terminal_row;
    }

    if (terminal_row >= VGA_HEIGHT)
    {
        terminal_scroll();
        terminal_delete_last_line();
        terminal_row = VGA_HEIGHT - 1u;
    }
}

void terminal_write_number(long num, uint8_t base)
{
    char buf[65];
    int i = 0;
    uint64_t n;

    if (base < 2u || base > 16u)
        return;

    if (num < 0)
    {
        terminal_putchar('-');
        n = (uint64_t) (-num);
    }
    else
    {
        n = (uint64_t) num;
    }

    if (n == 0)
    {
        terminal_putchar('0');
        return;
    }

    while (n > 0)
    {
        buf[i++] = "0123456789ABCDEF"[n % base];
        n /= base;
    }

    while (--i >= 0)
        terminal_putchar(buf[i]);
}

void terminal_write_string(const char *data)
{
    for (; data && *data; ++data)
        terminal_putchar(*data);
}
