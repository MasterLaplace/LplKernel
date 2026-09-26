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
 * @file vga.h
 * @brief VGA text-mode colours and character entries.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2024-05-31
 **************************************************************************/

#ifndef ARCH_I386_VGA_H_
#define ARCH_I386_VGA_H_

#include <stdint.h>

/** Hardware text mode color constants. */
typedef enum VGA_COLOR {
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

static inline uint8_t vga_entry_color(const VGA_COLOR fg, const VGA_COLOR bg) { return fg | bg << 4; }

static inline uint16_t vga_entry(const uint8_t uc, const uint8_t color)
{
    return (uint16_t) uc | (uint16_t) color << 8;
}

#endif /* !ARCH_I386_VGA_H_ */
