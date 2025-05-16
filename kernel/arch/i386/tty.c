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
    const uint16_t index = col * VGA_WIDTH + row;
    terminal_buffer[index] = vga_entry(c, color);
}

static inline void terminal_reset_pos(void)
{
    terminal_row = 0u;
    terminal_column = 0u;
}

static inline void terminal_clear(void)
{
    for (uint16_t col = 0; col < VGA_HEIGHT; ++col)
    {
        for (uint16_t row = 0; row < VGA_WIDTH; ++row)
        {
            const uint16_t index = col * VGA_WIDTH + row;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

static inline void terminal_scroll(uint16_t line)
{
    for (uint32_t loop = line * (VGA_WIDTH * 2u) + 0xB8000u; loop < VGA_WIDTH * 2; ++loop)
    {
        uint8_t c = *(uint8_t *) loop;
        *(uint8_t *) (loop - (VGA_WIDTH * 2u)) = c;
    }
}

static inline void terminal_delete_last_line(void)
{
    for (uint16_t row = 0u; row < VGA_WIDTH * 2u; ++row)
    {
        terminal_buffer[(VGA_WIDTH * 2u) * (VGA_HEIGHT - 1) + row] = 0u;
    }
}

////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

void terminal_initialize(void)
{
    for (uint16_t col = 0u; col < VGA_HEIGHT; ++col)
    {
        for (uint16_t row = 0u; row < VGA_WIDTH; ++row)
        {
            const uint16_t index = VGA_WIDTH * col + row;
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
    case '\n':
        terminal_column = 0u;
        /* fallthrough */
    case '\v':
        ++terminal_row;
        break;
    case '\r': terminal_column = 0u; break;
    case '\t': terminal_column += 4u; break;
    default:
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        ++terminal_column;
        break;
    }

    if (terminal_column != VGA_WIDTH)
        return;

    terminal_column = 0u;
    ++terminal_row;

    if (terminal_row != VGA_HEIGHT)
        return;

    for (uint16_t line = 1u; line < VGA_HEIGHT; ++line)
        terminal_scroll(line);
    terminal_delete_last_line();
    terminal_row = VGA_HEIGHT - 1u;
}

void terminal_write_number(long num, uint8_t base)
{
    if (base < 2u || base > 16u)
        return;

    if (num < 0)
    {
        terminal_putchar('-');
        num = -num;
    }

    if (num > (long) (base - 1u))
        terminal_write_number(num / base, base);

    terminal_putchar("0123456789ABCDEF"[num % base]);
}

void terminal_write_string(const char *data)
{
    for (; data && *data; ++data)
        terminal_putchar(*data);
}
