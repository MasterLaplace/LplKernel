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
 * @file multiboot_info.h
 * @brief Multiboot information structures handed over by the bootloader.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2025-05-19
 **************************************************************************/

#ifndef MULTIBOOT_INFO_H_
#define MULTIBOOT_INFO_H_

#include <stdint.h>

/** Structure representing the boot device (field `boot_device` of `MultibootInfo`). */
typedef struct __attribute__((packed)) {
    uint8_t drive; /**< BIOS drive number (e.g., 0x80) */
    uint8_t part1; /**< top-level partition number */
    uint8_t part2; /**< sub-partition */
    uint8_t part3; /**< sub-sub-partition */
} BootDevice_t;

/** Structure representing a module loaded by the bootloader. */
typedef struct __attribute__((packed)) {
    uint32_t mod_start; /**< physical start address */
    uint32_t mod_end;   /**< physical end address */
    uint32_t string;    /**< physical address of zero-terminated string */
    uint32_t reserved;  /**< must be 0 */
} Module_t;

/** a.out symbol table. */
typedef struct __attribute__((packed)) {
    uint32_t tabsize;
    uint32_t strsize;
    uint32_t addr;
    uint32_t reserved; /**< must be 0 */
} AOutSymbolTable_t;

/** ELF section header table. */
typedef struct __attribute__((packed)) {
    uint32_t num;   /**< number of entries */
    uint32_t size;  /**< size of each entry */
    uint32_t addr;  /**< physical address of section header table */
    uint32_t shndx; /**< index of the string table section */
} ELFSectionHeaderTable_t;

/** Memory map entry. */
typedef struct __attribute__((packed)) {
    uint32_t size;      /**< size of the entry excluding this field */
    uint64_t base_addr; /**< base physical address */
    uint64_t length;    /**< length of the region */
    uint32_t type;      /**< type of memory (1=usable, etc.) */
} MemoryMapEntry_t;

/** Drive info. */
typedef struct __attribute__((packed)) {
    uint32_t size; /**< total size of this structure */
    uint8_t drive_number;
    uint8_t drive_mode; /**< 0=CHS, 1=LBA */
    uint16_t drive_cylinders;
    uint8_t drive_heads;
    uint8_t drive_sectors;
    uint16_t drive_ports[]; /**< variable-sized array, terminated with 0 */
} DriveInfo_t;

/** APM table (Advanced Power Management). */
typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t cseg;
    uint32_t offset;
    uint16_t cseg_16;
    uint16_t dseg;
    uint16_t flags;
    uint16_t cseg_len;
    uint16_t cseg_16_len;
    uint16_t dseg_len;
} APMTable_t;

/** Color of a palette entry (framebuffer in indexed mode). */
typedef struct __attribute__((packed)) {
    uint8_t red_value;
    uint8_t green_value;
    uint8_t blue_value;
} FramebufferPaletteColor_t;

/** Main structure: `multiboot_info`. */
typedef struct __attribute__((packed)) {
    uint32_t flags; /**< Which of the fields below are valid, one bit per group. */

    uint32_t mem_lower; /**< Valid if flags bit 0 is set. */
    uint32_t mem_upper; /**< Valid if flags bit 0 is set. */

    BootDevice_t *boot_device; /**< Valid if flags bit 1 is set. */

    uint32_t cmdline; /**< Valid if flags bit 2 is set. */

    uint32_t mods_count; /**< Valid if flags bit 3 is set. */
    Module_t *mods_addr; /**< Valid if flags bit 3 is set. */

    /** Valid if flags bit 4 (a.out) or bit 5 (ELF) is set. */
    union {
        AOutSymbolTable_t aout_sym;
        ELFSectionHeaderTable_t elf_sec;
    };

    uint32_t mmap_length;        /**< Valid if flags bit 6 is set. */
    MemoryMapEntry_t *mmap_addr; /**< Valid if flags bit 6 is set. */

    uint32_t drives_length;   /**< Valid if flags bit 7 is set. */
    DriveInfo_t *drives_addr; /**< Valid if flags bit 7 is set. */

    uint32_t config_table; /**< Valid if flags bit 8 is set. */

    uint32_t boot_loader_name; /**< Valid if flags bit 9 is set. */

    APMTable_t *apm_table; /**< Valid if flags bit 10 is set. */

    uint32_t vbe_control_info;  /**< Valid if flags bit 11 is set. */
    uint32_t vbe_mode_info;     /**< Valid if flags bit 11 is set. */
    uint16_t vbe_mode;          /**< Valid if flags bit 11 is set. */
    uint16_t vbe_interface_seg; /**< Valid if flags bit 11 is set. */
    uint16_t vbe_interface_off; /**< Valid if flags bit 11 is set. */
    uint16_t vbe_interface_len; /**< Valid if flags bit 11 is set. */

    uint64_t framebuffer_addr;   /**< Valid if flags bit 12 is set. */
    uint32_t framebuffer_pitch;  /**< Valid if flags bit 12 is set. */
    uint32_t framebuffer_width;  /**< Valid if flags bit 12 is set. */
    uint32_t framebuffer_height; /**< Valid if flags bit 12 is set. */
    uint8_t framebuffer_bpp;     /**< Valid if flags bit 12 is set. */
    uint8_t framebuffer_type;    /**< Valid if flags bit 12 is set. */

    union {
        struct __attribute__((packed)) {
            FramebufferPaletteColor_t *framebuffer_palette_addr;
            uint16_t framebuffer_palette_num_colors;
        } indexed_color_t;

        struct __attribute__((packed)) {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        } direct_color_t;
    };
} MultibootInfo_t;

#endif /* !MULTIBOOT_INFO_H_ */
