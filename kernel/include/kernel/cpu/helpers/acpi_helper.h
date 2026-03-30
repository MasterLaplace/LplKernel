/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** acpi_helper
*/

#ifndef KERNEL_CPU_ACPI_HELPER_H
#define KERNEL_CPU_ACPI_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_acpi_madt_info(Serial_t *serial);
extern void write_acpi_ioapics_info(Serial_t *serial);
extern void write_acpi_isos_info(Serial_t *serial);
extern void write_acpi_isa_routing_info(Serial_t *serial);

#endif /* KERNEL_CPU_ACPI_HELPER_H */
