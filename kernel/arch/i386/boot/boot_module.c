/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** Boot module lookup — resolving a multiboot module to bytes we can read.
*/
#include <kernel/boot/boot_module.h>
#include <kernel/boot/multiboot_info.h>
#include <kernel/cpu/paging.h>

/** @brief Multiboot flag bit indicating the module list is present. */
#define MULTIBOOT_FLAG_MODULES (1u << 3)

extern MultibootInfo_t *multiboot_info;

/** @brief Physical to kernel-virtual, through the direct map. */
static inline const void *boot_module_phys_to_virt(uint32_t phys_addr)
{
    return (const void *) (uintptr_t) (phys_addr + KERNEL_VIRTUAL_BASE);
}

/** @brief Length of a NUL-terminated string, bounded so a corrupt one cannot hang us. */
static uint32_t boot_module_string_length(const char *text, uint32_t limit)
{
    uint32_t length = 0u;

    while (length < limit && text[length] != '\0')
        ++length;
    return length;
}

/**
 * @brief Does @p text end with @p suffix?
 */
static bool boot_module_ends_with(const char *text, uint32_t text_length, const char *suffix, uint32_t suffix_length)
{
    if (suffix_length > text_length)
        return false;

    const char *tail = text + (text_length - suffix_length);

    for (uint32_t i = 0u; i < suffix_length; ++i)
        if (tail[i] != suffix[i])
            return false;
    return true;
}

uint32_t boot_module_count(void)
{
    if (!multiboot_info)
        return 0u;
    if (!(multiboot_info->flags & MULTIBOOT_FLAG_MODULES))
        return 0u;
    if (!multiboot_info->mods_addr)
        return 0u;
    return multiboot_info->mods_count;
}

bool boot_module_find(const char *suffix, const uint8_t **out_bytes, uint32_t *out_size)
{
    if (!suffix || !out_bytes || !out_size)
        return false;

    const uint32_t count = boot_module_count();

    if (count == 0u)
        return false;

    const Module_t *modules =
        (const Module_t *) boot_module_phys_to_virt((uint32_t) (uintptr_t) multiboot_info->mods_addr);
    const uint32_t suffix_length = boot_module_string_length(suffix, 64u);

    for (uint32_t i = 0u; i < count; ++i)
    {
        const uint32_t start = modules[i].mod_start;
        const uint32_t end = modules[i].mod_end;

        if (end <= start)
            continue;

        if (modules[i].string != 0u)
        {
            const char *name = (const char *) boot_module_phys_to_virt(modules[i].string);
            const uint32_t name_length = boot_module_string_length(name, 256u);

            if (!boot_module_ends_with(name, name_length, suffix, suffix_length))
                continue;
        }
        else if (count > 1u)
        {
            /* Unnamed, and not the only candidate: refuse to guess which one
               the caller meant rather than hand back the wrong cartridge. */
            continue;
        }

        *out_bytes = (const uint8_t *) boot_module_phys_to_virt(start);
        *out_size = end - start;
        return true;
    }
    return false;
}
