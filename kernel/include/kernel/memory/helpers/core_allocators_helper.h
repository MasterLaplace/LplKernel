/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** core_allocators_helper
*/

#ifndef KERNEL_MEMORY_CORE_ALLOCATORS_HELPER_H
#define KERNEL_MEMORY_CORE_ALLOCATORS_HELPER_H

#include <kernel/drivers/serial.h>
#include <stdbool.h>

extern void write_core_allocators_info(Serial_t *serial, bool frame_arena_ok, bool stack_allocator_ok,
                                       bool pool_allocator_ok, bool pinned_ok);
extern void write_ring_buffer_info(Serial_t *serial, bool ring_buffer_ok);

#endif /* KERNEL_MEMORY_CORE_ALLOCATORS_HELPER_H */
