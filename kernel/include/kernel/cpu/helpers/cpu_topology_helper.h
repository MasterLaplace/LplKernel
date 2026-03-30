/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** cpu_topology_helper
*/

#ifndef KERNEL_CPU_TOPOLOGY_HELPER_H
#define KERNEL_CPU_TOPOLOGY_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_cpu_topology_info(Serial_t *serial);
extern void write_cpu_topology_madt_sync_info(Serial_t *serial);

#endif /* KERNEL_CPU_TOPOLOGY_HELPER_H */
