/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** ioapic_helper
*/

#ifndef KERNEL_CPU_IOAPIC_HELPER_H
#define KERNEL_CPU_IOAPIC_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_ioapic_scaffold_info(Serial_t *serial);
extern void write_ioapic_routes_info(Serial_t *serial);

#endif /* KERNEL_CPU_IOAPIC_HELPER_H */
