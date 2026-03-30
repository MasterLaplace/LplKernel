/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** heap_helper
*/

#ifndef KERNEL_MEMORY_HEAP_HELPER_H
#define KERNEL_MEMORY_HEAP_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_heap_info(Serial_t *serial);
extern void write_heap_extended_info(Serial_t *serial);

#endif /* KERNEL_MEMORY_HEAP_HELPER_H */
