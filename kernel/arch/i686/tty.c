#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>

#include "vga.h"

////////////////////////////////////////////////////////////
// Private members of the terminal module
////////////////////////////////////////////////////////////

static const uint16_t VGA_WIDTH = 80;
static const uint16_t VGA_HEIGHT = 25;

static uint16_t terminal_row = 0;
static uint16_t terminal_column = 0;
static uint8_t terminal_color = 0x0F;
static volatile uint16_t * const terminal_buffer = (uint16_t*) 0xB8000;


////////////////////////////////////////////////////////////
// Private functions of the terminal module
////////////////////////////////////////////////////////////

static inline void terminal_putentryat(unsigned char c, uint8_t color, uint16_t x, uint16_t y)
{
    const uint16_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry(c, color);
}

static inline void terminal_reset_pos(void)
{
    terminal_row = 0;
    terminal_column = 0;
}

static inline void terminal_clear(void)
{
    for (uint16_t y = 0; y < VGA_HEIGHT; ++y)
    {
        for (uint16_t x = 0; x < VGA_WIDTH; ++x)
        {
            const uint16_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

static inline void terminal_scroll(int line)
{
	int loop;
	char c;

	for(loop = line * (VGA_WIDTH * 2) + 0xB8000; loop < VGA_WIDTH * 2; loop++)
    {
        c = *(char*)loop;
        *(char*)(loop - (VGA_WIDTH * 2)) = c;
	}
}

static inline void terminal_delete_last_line(void)
{
	int x;
    char *ptr;

	for(x = 0; x < VGA_WIDTH * 2; x++) {
        ptr = (char *)(0xB8000 + (VGA_WIDTH * 2) * (VGA_HEIGHT - 1) + x);
		*ptr = 0;
	}
}


////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

void terminal_initialize(void)
{
    for (uint16_t col = 0; col < VGA_HEIGHT; ++col)
    {
        for (uint16_t row = 0; row < VGA_WIDTH; ++row)
        {
            const uint16_t index = VGA_WIDTH * col + row;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

void terminal_setcolor(uint8_t color)
{
    terminal_color = color;
}

void terminal_putchar(char c)
{
    switch (c)
    {
    case '\n':
        ++terminal_row;
        terminal_column = 0;
        break;
    case '\r':
        terminal_column = 0;
        break;
    case '\t':
        terminal_column += 4;
        break;
    default:
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        ++terminal_column;
        break;
    }

    if (terminal_column != VGA_WIDTH)
        return;

    terminal_column = 0;
    ++terminal_row;

    if (terminal_row != VGA_HEIGHT)
        return;

    for (int line = 1; line <= VGA_HEIGHT - 1; line++)
        terminal_scroll(line);
    terminal_delete_last_line();
    terminal_row = VGA_HEIGHT - 1;
}

void terminal_write_number(long num, uint8_t base)
{
    if (base < 2 || base > 16)
        return;

    if (num < 0)
    {
        terminal_putchar('-');
        num = -num;
    }

    if (num > base - 1)
        terminal_write_number(num / base, base);

    terminal_putchar("0123456789ABCDEF"[num % base]);
}

void terminal_write_string(const char *data)
{
    for (; data && *data; ++data)
        terminal_putchar(*data);
}
