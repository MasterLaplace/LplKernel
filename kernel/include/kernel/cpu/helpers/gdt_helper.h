/*
** EPITECH PROJECT, 2025
** LplKernel [WSL : Ubuntu]
** File description:
** gdt_helper
*/

#ifndef KERNEL_CPU_GLOBAL_DESCRIPTOR_TABLE_HELPER_H
#define KERNEL_CPU_GLOBAL_DESCRIPTOR_TABLE_HELPER_H

#include <kernel/cpu/gdt.h>
#include <kernel/drivers/serial.h>
#include <kernel/drivers/tty.h>

////////////////////////////////////////////////////////////
// Public API functions of the GDT helper module
////////////////////////////////////////////////////////////

/**
 * @brief Print a decoded Global Descriptor Table to terminal output.
 */
extern void print_global_descriptor_table(GlobalDescriptorTable_t *gdt);

//********************************************************//

/**
 * @brief Write a decoded Global Descriptor Table to serial output.
 */
extern void write_global_descriptor_table(Serial_t *serial, GlobalDescriptorTable_t *gdt);

#endif /* KERNEL_CPU_GLOBAL_DESCRIPTOR_TABLE_HELPER_H */
