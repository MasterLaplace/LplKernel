#ifndef MULTIBOOT_INFO_HELPER_H_
#define MULTIBOOT_INFO_HELPER_H_

#include <kernel/boot/multiboot_info.h>
#include <kernel/drivers/serial.h>
#include <kernel/drivers/tty.h>

////////////////////////////////////////////////////////////
// Public functions of the multiboot info helper module API
////////////////////////////////////////////////////////////

extern void print_multiboot_info_boot_device(BootDevice_t *boot_device);

extern void print_multiboot_info_module(uint32_t kernel_start, Module_t *module);

extern void print_multiboot_info_aout_symbol_table(AOutSymbolTable_t *aout_sym);

extern void print_multiboot_info_elf_section_header_table(ELFSectionHeaderTable_t *elf_sec);

extern void print_multiboot_info_memory_map(uint32_t kernel_start, MultibootInfo_t *mbi);

extern void print_multiboot_info_drive_info(DriveInfo_t *drive_info);

extern void print_multiboot_info_apm_table(APMTable_t *apm_table);

extern void print_multiboot_info_framebuffer_palette_color(FramebufferPaletteColor_t *color);

extern void print_multiboot_info_vbe_info(MultibootInfo_t *mbi);

extern void print_multiboot_info_framebuffer(MultibootInfo_t *mbi);

extern void print_multiboot_info(uint32_t kernel_start, MultibootInfo_t *mbi);

//********************************************************//

extern void write_multiboot_info_boot_device(Serial_t *serial, BootDevice_t *boot_device);

extern void write_multiboot_info_module(Serial_t *serial, uint32_t kernel_start, Module_t *module);

extern void write_multiboot_info_aout_symbol_table(Serial_t *serial, AOutSymbolTable_t *aout_sym);

extern void write_multiboot_info_elf_section_header_table(Serial_t *serial, ELFSectionHeaderTable_t *elf_sec);

extern void write_multiboot_info_memory_map(Serial_t *serial, uint32_t kernel_start, MultibootInfo_t *mbi);

extern void write_multiboot_info_drive_info(Serial_t *serial, DriveInfo_t *drive_info);

extern void write_multiboot_info_apm_table(Serial_t *serial, APMTable_t *apm_table);

extern void write_multiboot_info_framebuffer_palette_color(Serial_t *serial, FramebufferPaletteColor_t *color);

extern void write_multiboot_info_vbe_info(Serial_t *serial, MultibootInfo_t *mbi);

extern void write_multiboot_info_framebuffer(Serial_t *serial, MultibootInfo_t *mbi);

extern void write_multiboot_info(Serial_t *serial, uint32_t kernel_start, MultibootInfo_t *mbi);

#endif /* !MULTIBOOT_INFO_HELPER_H_ */
