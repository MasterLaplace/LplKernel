/*
** LAPLACE&ME PROJECT, 2024
** Laplace [KERNEL_TTY]
** Author:
** Master Laplace
** File description:
** KERNEL_TTY
*/

#define KERNEL_TTY_IMPLEMENTATION
#ifndef KERNEL_TTY_H_
    #define KERNEL_TTY_H_

#include <stddef.h>
#include <stdint.h>

#if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#elif !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

////////////////////////////////////////////////////////////
// Public functions of the terminal module API
////////////////////////////////////////////////////////////

void terminal_initialize(void);

void terminal_setcolor(uint8_t color);

void terminal_putchar(char c);

void terminal_write_number(long num, uint8_t base);

void terminal_write_string(const char *data);

#endif /* !KERNEL_TTY_H_ */
