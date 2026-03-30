/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** keyboard_helper
*/

#ifndef KERNEL_DRIVERS_KEYBOARD_HELPER_H
#define KERNEL_DRIVERS_KEYBOARD_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_keyboard_runtime_info(Serial_t *serial);

#endif /* KERNEL_DRIVERS_KEYBOARD_HELPER_H */
