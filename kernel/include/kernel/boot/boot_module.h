/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
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

#endif /* BOOT_MODULE_H_ */
