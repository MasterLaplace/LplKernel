/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** section_protection_helper
*/

#ifndef KERNEL_MEMORY_SECTION_PROTECTION_HELPER_H
#define KERNEL_MEMORY_SECTION_PROTECTION_HELPER_H

#include <kernel/drivers/serial.h>
#include <stdbool.h>

extern void write_section_protection_info(Serial_t *serial, bool applied);

#endif /* KERNEL_MEMORY_SECTION_PROTECTION_HELPER_H */
