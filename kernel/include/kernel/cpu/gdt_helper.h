/*
** EPITECH PROJECT, 2025
** LplKernel [WSL : Ubuntu]
** File description:
** gdt_helper
*/

#ifndef GDT_HELPER_H_
#define GDT_HELPER_H_

#include <kernel/cpu/gdt.h>
#include <kernel/drivers/serial.h>
#include <kernel/drivers/tty.h>

////////////////////////////////////////////////////////////
// Public functions of the GDT helper module API
////////////////////////////////////////////////////////////

extern void print_global_descriptor_table(GlobalDescriptorTable_t *gdt);

//********************************************************//

extern void write_global_descriptor_table(Serial_t *serial, GlobalDescriptorTable_t *gdt);

#endif /* GDT_HELPER_H_ */
