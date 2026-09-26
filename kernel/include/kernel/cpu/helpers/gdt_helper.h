/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2025 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file gdt_helper.h
 * @brief Terminal and serial dumps of the Global Descriptor Table.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-10-17
 **************************************************************************/

#ifndef KERNEL_CPU_GLOBAL_DESCRIPTOR_TABLE_HELPER_H
#define KERNEL_CPU_GLOBAL_DESCRIPTOR_TABLE_HELPER_H

#include <kernel/cpu/gdt.h>
#include <kernel/drivers/serial.h>
#include <kernel/drivers/tty.h>

/**
 * @brief Print a decoded Global Descriptor Table to terminal output.
 */
extern void print_global_descriptor_table(GlobalDescriptorTable_t *gdt);

/**
 * @brief Write a decoded Global Descriptor Table to serial output.
 */
extern void write_global_descriptor_table(Serial_t *serial, GlobalDescriptorTable_t *gdt);

#endif /* KERNEL_CPU_GLOBAL_DESCRIPTOR_TABLE_HELPER_H */
