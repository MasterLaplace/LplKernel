/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2024 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file tty.h
 * @brief VGA text-mode terminal.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2024-05-31
 **************************************************************************/

#ifndef KERNEL_DRIVERS_TTY_H
#define KERNEL_DRIVERS_TTY_H

#include <stddef.h>
#include <stdint.h>

#if defined(__linux__)
#    error "This code must be compiled with a cross-compiler"
#elif !defined(__i386__)
#    error "This code must be compiled with an x86-elf compiler"
#endif

#include "vga.h"

extern void terminal_initialize(void);

extern void terminal_reset_pos(void);

extern void terminal_clear(void);

extern void terminal_setcolor(uint8_t color);

extern uint8_t terminal_getcolor(void);

extern void terminal_putchar(char c);

extern void terminal_write_number(long num, uint8_t base);

extern void terminal_write_string(const char *data);

#endif /* KERNEL_DRIVERS_TTY_H */
