#ifndef KERNEL_BOOT_MULTIBOOT_INFO_HELPER_H
#define KERNEL_BOOT_MULTIBOOT_INFO_HELPER_H

#include <kernel/boot/multiboot_info.h>
#include <kernel/drivers/serial.h>
#include <kernel/drivers/tty.h>

////////////////////////////////////////////////////////////
// Public API functions of the multiboot info helper module
////////////////////////////////////////////////////////////

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

//********************************************************//

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
