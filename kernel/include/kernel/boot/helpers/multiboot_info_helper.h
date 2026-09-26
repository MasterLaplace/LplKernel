/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2025 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file multiboot_info_helper.h
 * @brief Reports of the multiboot information block, on the terminal and on serial.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-10-05
 **************************************************************************/

#ifndef KERNEL_BOOT_MULTIBOOT_INFO_HELPER_H
#define KERNEL_BOOT_MULTIBOOT_INFO_HELPER_H

#include <kernel/boot/multiboot_info.h>
#include <kernel/drivers/serial.h>
#include <kernel/drivers/tty.h>

/**
 * @brief Print boot device information to terminal output.
 */
extern void print_multiboot_info_boot_device(BootDevice_t *boot_device);

/**
 * @brief Print multiboot module information to terminal output.
 */
extern void print_multiboot_info_module(uint32_t kernel_start, Module_t *module);

/**
 * @brief Print a.out symbol table information to terminal output.
 */
extern void print_multiboot_info_aout_symbol_table(AOutSymbolTable_t *aout_sym);

/**
 * @brief Print ELF section header table information to terminal output.
 */
extern void print_multiboot_info_elf_section_header_table(ELFSectionHeaderTable_t *elf_sec);

/**
 * @brief Print memory map entries to terminal output.
 *
 * @details
 * The memory map address is interpreted as a physical address in the low-memory
 * identity window and translated with @p kernel_start.
 */
extern void print_multiboot_info_memory_map(uint32_t kernel_start, MultibootInfo_t *mbi);

/**
 * @brief Print BIOS drive information to terminal output.
 */
extern void print_multiboot_info_drive_info(DriveInfo_t *drive_info);

/**
 * @brief Print APM table information to terminal output.
 */
extern void print_multiboot_info_apm_table(APMTable_t *apm_table);

/**
 * @brief Print framebuffer palette color information to terminal output.
 */
extern void print_multiboot_info_framebuffer_palette_color(FramebufferPaletteColor_t *color);

/**
 * @brief Print VBE information to terminal output when present.
 */
extern void print_multiboot_info_vbe_info(MultibootInfo_t *mbi);

/**
 * @brief Print framebuffer information to terminal output when present.
 */
extern void print_multiboot_info_framebuffer(MultibootInfo_t *mbi);

/**
 * @brief Print the complete multiboot information structure to terminal output.
 */
extern void print_multiboot_info(uint32_t kernel_start, MultibootInfo_t *mbi);

/**
 * @brief Write boot device information to serial output.
 */
extern void write_multiboot_info_boot_device(Serial_t *serial, BootDevice_t *boot_device);

/**
 * @brief Write module information to serial output.
 */
extern void write_multiboot_info_module(Serial_t *serial, uint32_t kernel_start, Module_t *module);

/**
 * @brief Write a.out symbol table information to serial output.
 */
extern void write_multiboot_info_aout_symbol_table(Serial_t *serial, AOutSymbolTable_t *aout_sym);

/**
 * @brief Write ELF section header table information to serial output.
 */
extern void write_multiboot_info_elf_section_header_table(Serial_t *serial, ELFSectionHeaderTable_t *elf_sec);

/**
 * @brief Write memory map entries to serial output.
 */
extern void write_multiboot_info_memory_map(Serial_t *serial, uint32_t kernel_start, MultibootInfo_t *mbi);

/**
 * @brief Write drive information to serial output.
 */
extern void write_multiboot_info_drive_info(Serial_t *serial, DriveInfo_t *drive_info);

/**
 * @brief Write APM table information to serial output.
 */
extern void write_multiboot_info_apm_table(Serial_t *serial, APMTable_t *apm_table);

/**
 * @brief Write framebuffer palette color information to serial output.
 */
extern void write_multiboot_info_framebuffer_palette_color(Serial_t *serial, FramebufferPaletteColor_t *color);

/**
 * @brief Write VBE information to serial output when present.
 */
extern void write_multiboot_info_vbe_info(Serial_t *serial, MultibootInfo_t *mbi);

/**
 * @brief Write framebuffer information to serial output when present.
 */
extern void write_multiboot_info_framebuffer(Serial_t *serial, MultibootInfo_t *mbi);

/**
 * @brief Write complete multiboot information to serial output.
 */
extern void write_multiboot_info(Serial_t *serial, uint32_t kernel_start, MultibootInfo_t *mbi);

#endif /* KERNEL_BOOT_MULTIBOOT_INFO_HELPER_H */
