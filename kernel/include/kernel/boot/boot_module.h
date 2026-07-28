/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Boot modules — the cartridge slot.
**
** GRUB can hand the kernel arbitrary files alongside the image (multiboot
** modules). That is how a game reaches a target with no filesystem: the ISO
** carries `game.lplpak`, GRUB loads it into RAM, and the kernel gets an address
** and a length. A console loads a cartridge; it does not mount a disk.
**
** The bytes are only safe to read because the PMM withholds every page a module
** covers (see pmm_is_boot_module_page). Without that reservation the memory map
** still reports the module's RAM as available and the heap would eventually
** allocate over it.
*/
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
