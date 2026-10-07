/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file boot_module.h
 * @brief Boot modules: the cartridge slot.
 *
 * GRUB can hand the kernel arbitrary files alongside the image (multiboot
 * modules). That is how a game reaches a target with no filesystem: the ISO
 * carries `game.lplpak`, GRUB loads it into RAM, and the kernel gets an address
 * and a length. A console loads a cartridge; it does not mount a disk.
 *
 * The bytes are only safe to read because the PMM withholds every page a module
 * covers (see pmm_is_boot_module_page). Without that reservation the memory map
 * still reports the module's RAM as available and the heap would eventually
 * allocate over it.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-07-28
 **************************************************************************/

#ifndef BOOT_MODULE_H_
#define BOOT_MODULE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Bytes kept of the kernel's command line, its terminating NUL included. */
#define BOOT_COMMAND_LINE_CAPACITY 256u

/**
 * @brief Locates a boot module whose command line ends with @p suffix.
 *
 * Matching on a suffix rather than an exact name keeps the lookup independent
 * of how GRUB spells the path it was given ("/boot/game.lplpak" vs
 * "game.lplpak").
 *
 * @note A module with no command line matches only when it is the only module: with
 *       several candidates the lookup refuses to guess which one the caller meant rather
 *       than hand back the wrong cartridge.
 *
 * @param suffix     Text the module's command line must end with (e.g. ".lplpak").
 * @param out_bytes  Receives a kernel-virtual pointer to the module contents.
 * @param out_size   Receives the module length in bytes.
 * @return true when a matching module was found and is non-empty.
 */
bool boot_module_find(const char *suffix, const uint8_t **out_bytes, uint32_t *out_size);

/**
 * @brief Number of modules the bootloader passed to us.
 * @return The module count (0 when the bootloader supplied none).
 */
uint32_t boot_module_count(void);

/**
 * @brief Copies the kernel's command line out of the memory the bootloader left it in.
 *
 * @details The physical memory manager reserves the modules' pages but not the command line's, so
 *          the text is overwritten once those pages are handed out. Called once, before the physical
 *          memory manager starts; the copy is cut at BOOT_COMMAND_LINE_CAPACITY - 1 characters.
 */
void boot_command_line_capture(void);

/**
 * @brief The kernel's own command line, as boot_command_line_capture() copied it.
 *
 * @return The NUL-terminated text, or NULL when the bootloader passed none.
 */
const char *boot_command_line(void);

#endif /* BOOT_MODULE_H_ */
