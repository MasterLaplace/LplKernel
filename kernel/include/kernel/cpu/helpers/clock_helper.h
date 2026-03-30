/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** clock_helper
*/

#ifndef KERNEL_CPU_CLOCK_HELPER_H
#define KERNEL_CPU_CLOCK_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_clock_info(Serial_t *serial);

#endif /* KERNEL_CPU_CLOCK_HELPER_H */
