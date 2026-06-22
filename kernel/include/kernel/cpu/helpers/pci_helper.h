/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** pci_helper
*/

#ifndef KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_H
#define KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_H

#include <kernel/drivers/serial.h>

/**
 * @brief Print every PCI device recorded by the last enumeration scan to serial.
 *
 * Call peripheral_component_interconnect_scan() first.
 */
extern void write_peripheral_component_interconnect_info(Serial_t *serial);

#endif /* KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_H */
