/*
** LAPLACE&ME PROJECT, 2024
** Laplace [KERNEL_TTY]
** Author:
** Master Laplace
** File description:
** KERNEL_TTY
*/

#ifndef KERNEL_TTY_H_
#define KERNEL_TTY_H_

#include <stddef.h>
#include <stdint.h>

#if defined(__linux__)
#    error "You are not using a cross-compiler, you will most certainly run into trouble"
#elif !defined(__i386__)
#    error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

#include "vga.h"

////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

extern void terminal_initialize(void);

extern void terminal_setcolor(const uint8_t color);

extern void terminal_putchar(const char c);

extern void terminal_write_number(long num, const uint8_t base);

extern void terminal_write_string(const char *data);

#endif /* !KERNEL_TTY_H_ */
