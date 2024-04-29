#ifndef TERMINAL_H_
    #define TERMINAL_H_

#include "libc.h"

#if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#elif !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

////////////////////////////////////////////////////////////
// Types and constants of the terminal module
////////////////////////////////////////////////////////////

/* Hardware text mode color constants. */
typedef enum VGA_COLOR KERNEL_CPP11(: uint8_t)
{
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
} VGA_COLOR;

////////////////////////////////////////////////////////////
// Members of the terminal module
////////////////////////////////////////////////////////////

const uint16_t VGA_WIDTH = 80;
const uint16_t VGA_HEIGHT = 25;

uint16_t terminal_row = 0;
uint16_t terminal_column = 0;
uint8_t terminal_color = 0x0F;
volatile uint16_t *terminal_buffer = (uint16_t*) 0xB8000;


////////////////////////////////////////////////////////////
// Private functions of the terminal module
////////////////////////////////////////////////////////////

static inline uint8_t vga_entry_color(VGA_COLOR fg, VGA_COLOR bg);

static inline uint16_t vga_entry(unsigned char uc, uint8_t color);

static inline void terminal_putentryat(char c, uint8_t color, uint16_t x, uint16_t y);

static inline void terminal_reset_pos(void);

static inline void terminal_clear(void);

static inline void terminal_scroll_up(void);


////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

void terminal_initialize(void);

void terminal_setcolor(uint8_t color);

void terminal_putchar(char c);

void terminal_write_string(const char *data);

#endif /* !TERMINAL_H_ */


////////////////////////////////////////////////////////////////////////////////
/*                               IMPLEMENTATION                               */
////////////////////////////////////////////////////////////////////////////////
#ifdef TERMINAL_IMPLEMENTATION
#ifndef TERMINAL_C_ONCE
    #define TERMINAL_C_ONCE

////////////////////////////////////////////////////////////
// Private functions of the terminal module
////////////////////////////////////////////////////////////

static inline uint8_t vga_entry_color(VGA_COLOR fg, VGA_COLOR bg)
{
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color)
{
    return (uint16_t) uc | (uint16_t) color << 8;
}

static inline void terminal_putentryat(char c, uint8_t color, uint16_t x, uint16_t y)
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

static inline void terminal_scroll_up(void)
{
    for (uint16_t y = 0; y < VGA_HEIGHT - 1; ++y)
    {
        for (uint16_t x = 0; x < VGA_WIDTH; ++x)
        {
            const uint16_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = terminal_buffer[index + VGA_WIDTH];
        }
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

    if (terminal_column == VGA_WIDTH)
    {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT)
        {
            terminal_row = 0;
        }
    }
}

void terminal_write_string(const char *data)
{
    for (; data && *data; ++data)
        terminal_putchar(*data);
}

#endif /* !TERMINAL_C_ONCE */
#endif /* !TERMINAL_IMPLEMENTATION */
