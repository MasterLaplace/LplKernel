#include <kernel/boot/boot_module.h>
#include <kernel/boot/helpers/multiboot_info_helper.h>
#include <kernel/cpu/paging.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#    error "This code assumes little-endian (x86). Update multiboot parsing for big-endian targets."
#endif

static inline uint8_t read_little_endian_u8(const uint8_t *p) { return p[0]; }

static inline uint16_t read_little_endian_u16(const uint8_t *p) { return (uint16_t) p[0] | ((uint16_t) p[1] << 8); }

static inline uint32_t read_little_endian_u32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static inline uint64_t read_little_endian_u64(const uint8_t *p)
{
    return (uint64_t) read_little_endian_u32(p) | ((uint64_t) read_little_endian_u32(p + 4) << 32);
}

static inline void serial_write_le_u8(Serial_t *s, const uint8_t *p) { serial_write_char(s, *p); }

/**
 * @brief Physical to virtual for whatever the multiboot block points to.
 *
 * GRUB leaves its structures below the first MiB, but QEMU's `-kernel` leaves them past the kernel, and modules
 * load several MiB up. boot.S installs 16 page tables, so the higher-half direct map covers the first 64 MiB from
 * the very first instruction: that, not 1 MiB, is the reachable window here. Anything beyond it is refused rather
 * than faulted on.
 */
static inline void *physical_to_virtual_boot_mapped(uint32_t phys_addr, uint32_t kernel_start)
{
    return (phys_addr < KERNEL_BOOT_MAPPED_END) ? (void *) (uintptr_t) (phys_addr + kernel_start) : NULL;
}

/**
 * @brief Physical to virtual for a field of @p length bytes the multiboot block points to, wherever the boot map
 *        reaches it: NULL when it starts at 0 or ends past the map.
 */
static void *physical_range_boot_mapped(uint32_t phys_addr, uint32_t length, uint32_t kernel_start)
{
    if (phys_addr == 0u || phys_addr >= KERNEL_BOOT_MAPPED_END || length > KERNEL_BOOT_MAPPED_END - phys_addr)
        return NULL;
    return physical_to_virtual_boot_mapped(phys_addr, kernel_start);
}

/**
 * @brief The module descriptor array, reached through the boot mapping.
 *
 * @note mods_addr is a PHYSICAL address despite its pointer type. Nothing dereferenced it
 *       until a module was actually present (the cartridge), at which point reading it as a
 *       virtual pointer faulted.
 *
 * @param mbi          The multiboot information block.
 * @param kernel_start Offset of the boot mapping.
 * @return The array, or NULL when it lies outside the boot map.
 */
static Module_t *multiboot_modules(const MultibootInfo_t *mbi, uint32_t kernel_start)
{
    return (Module_t *) physical_to_virtual_boot_mapped((uint32_t) (uintptr_t) mbi->mods_addr, kernel_start);
}

void write_multiboot_info_boot_device(Serial_t *serial, BootDevice_t *boot_device)
{
    uint32_t device = (uint32_t) (long) boot_device;
    serial_write_string(serial, "Boot device: ");
    serial_write_hex32(serial, device);

    uint8_t drive = (device >> 24) & 0xFF;
    uint8_t part1 = (device >> 16) & 0xFF;
    uint8_t part2 = (device >> 8) & 0xFF;
    uint8_t part3 = device & 0xFF;

    serial_write_string(serial, " (Drive: ");
    serial_write_int(serial, drive);
    serial_write_string(serial, ", Partitions: ");
    serial_write_int(serial, part1);
    serial_write_string(serial, ", ");
    serial_write_int(serial, part2);
    serial_write_string(serial, ", ");
    serial_write_int(serial, part3);
    serial_write_string(serial, ")\n");

    if (drive == 0x80)
        serial_write_string(serial, "  -> First hard disk (0x80)\n");
    else if (drive == 0x00)
        serial_write_string(serial, "  -> First floppy disk (0x00)\n");
    else if (drive >= 0x80)
    {
        serial_write_string(serial, "  -> Hard disk #");
        serial_write_int(serial, drive - 0x80 + 1);
        serial_write_string(serial, "\n");
    }
    else
        serial_write_string(serial, "  -> Unknown/Virtual device\n");
}

void write_multiboot_info_module(Serial_t *serial, uint32_t kernel_start, Module_t *module)
{
    serial_write_string(serial, "Module start: ");
    serial_write_hex32(serial, module->mod_start);
    serial_write_string(serial, "\nModule end: ");
    serial_write_hex32(serial, module->mod_end);
    serial_write_string(serial, "\nModule string address: ");
    serial_write_hex32(serial, module->string);
    serial_write_string(serial, "\n");
    char *s = (char *) physical_to_virtual_boot_mapped(module->string, kernel_start);

    if (s)
    {
        serial_write_string(serial, "Module string: ");
        serial_write_string(serial, s);
        serial_write_string(serial, "\n");
    }
    else
    {
        serial_write_string(serial, "Module string: (not accessible)\n");
    }
}

void write_multiboot_info_aout_symbol_table(Serial_t *serial, AOutSymbolTable_t *aout_sym)
{
    serial_write_string(serial, "Table size: ");
    serial_write_int(serial, aout_sym->tabsize);
    serial_write_string(serial, "\nString size: ");
    serial_write_int(serial, aout_sym->strsize);
    serial_write_string(serial, "\nAddress: ");
    serial_write_hex32(serial, aout_sym->addr);
    serial_write_string(serial, "\n");
}

void write_multiboot_info_elf_section_header_table(Serial_t *serial, ELFSectionHeaderTable_t *elf_sec)
{
    serial_write_string(serial, "Number of entries: ");
    serial_write_int(serial, elf_sec->num);
    serial_write_string(serial, "\nSize of each entry: ");
    serial_write_int(serial, elf_sec->size);
    serial_write_string(serial, "\nAddress: ");
    serial_write_hex32(serial, elf_sec->addr);
    serial_write_string(serial, "\nString table index: ");
    serial_write_int(serial, elf_sec->shndx);
    serial_write_string(serial, "\n");
}

void write_multiboot_info_memory_map(Serial_t *serial, uint32_t kernel_start, MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 6) && mbi->mmap_length > 0 && mbi->mmap_addr))
    {
        serial_write_string(serial, "No memory map provided.\n");
        return;
    }

    serial_write_string(serial, "Memory Map Length: ");
    serial_write_int(serial, mbi->mmap_length);
    serial_write_string(serial, " bytes\nMemory Map Address: ");
    serial_write_hex32(serial, (uint32_t) (long) mbi->mmap_addr);
    serial_write_string(serial, "\n");

    uint8_t *mmap_base =
        (uint8_t *) physical_range_boot_mapped((uint32_t) (uintptr_t) mbi->mmap_addr, mbi->mmap_length, kernel_start);
    if (!mmap_base)
    {
        serial_write_string(serial, "  Memory map not accessible (physical->virtual failed)\n");
        return;
    }

    uint32_t offset = 0;
    uint32_t index = 0;
    while (offset < mbi->mmap_length)
    {
        uint8_t *entry = mmap_base + offset;
        if (offset + 4 > mbi->mmap_length)
            break;
        uint32_t size = read_little_endian_u32(entry);
        if (size < 20)
            break;
        if (offset + 4 + size > mbi->mmap_length)
            break;

        uint8_t *payload = entry + 4;
        uint64_t base_addr = read_little_endian_u64(payload);
        uint64_t length = read_little_endian_u64(payload + 8);
        uint32_t type = read_little_endian_u32(payload + 16);

        serial_write_string(serial, "Memory Map Entry ");
        serial_write_int(serial, index);
        serial_write_string(serial, ":\n  Base Address: ");
        serial_write_hex64(serial, base_addr);
        serial_write_string(serial, "\n  Length: ");
        serial_write_hex64(serial, length);
        serial_write_string(serial, "\n  Type: ");
        serial_write_int(serial, type);
        const char *mem_type_desc = (type == 1) ? " (Available)" :
                                    (type == 2) ? " (Reserved)" :
                                    (type == 3) ? " (ACPI Reclaimable)" :
                                    (type == 4) ? " (ACPI NVS)" :
                                    (type == 5) ? " (Bad RAM)" :
                                                  " (Unknown/Reserved)";
        serial_write_string(serial, mem_type_desc);
        serial_write_string(serial, "\n");

        offset += 4 + size;
        ++index;
    }
}

void write_multiboot_info_drive_info(Serial_t *serial, DriveInfo_t *drive_info)
{
    serial_write_string(serial, "Structure size: ");
    serial_write_int(serial, drive_info->size);
    serial_write_string(serial, "\nDrive number: ");
    serial_write_int(serial, drive_info->drive_number);
    serial_write_string(serial, "\nDrive mode: ");
    serial_write_int(serial, drive_info->drive_mode);
    const char *mode_str = (drive_info->drive_mode == 0) ? " (CHS)" :
                           (drive_info->drive_mode == 1) ? " (LBA)" :
                                                           " (Unknown)";
    serial_write_string(serial, mode_str);
    serial_write_string(serial, "\nCylinders: ");
    serial_write_int(serial, drive_info->drive_cylinders);
    serial_write_string(serial, "\nHeads: ");
    serial_write_int(serial, drive_info->drive_heads);
    serial_write_string(serial, "\nSectors: ");
    serial_write_int(serial, drive_info->drive_sectors);

    serial_write_string(serial, "\nI/O Ports: ");
    uint8_t *base = (uint8_t *) drive_info;
    uint8_t *ptr = base + offsetof(DriveInfo_t, drive_ports);
    for (;;)
    {
        uint16_t port = read_little_endian_u16(ptr);
        if (port == 0)
            break;
        serial_write_hex32(serial, port);
        serial_write_string(serial, " ");
        ptr += 2;
    }
    serial_write_string(serial, "\n");
}

void write_multiboot_info_apm_table(Serial_t *serial, APMTable_t *apm_table)
{
    serial_write_string(serial, "Version: ");
    serial_write_int(serial, apm_table->version);
    serial_write_string(serial, "\nCSEG: ");
    serial_write_hex32(serial, apm_table->cseg);
    serial_write_string(serial, "\nOffset: ");
    serial_write_hex32(serial, apm_table->offset);
    serial_write_string(serial, "\nCSEG 16: ");
    serial_write_hex32(serial, apm_table->cseg_16);
    serial_write_string(serial, "\nDSEG: ");
    serial_write_hex32(serial, apm_table->dseg);
    serial_write_string(serial, "\nFlags: ");
    serial_write_hex32(serial, apm_table->flags);
    serial_write_string(serial, "\nCSEG Length: ");
    serial_write_int(serial, apm_table->cseg_len);
    serial_write_string(serial, "\nCSEG 16 Length: ");
    serial_write_int(serial, apm_table->cseg_16_len);
    serial_write_string(serial, "\nDSEG Length: ");
    serial_write_int(serial, apm_table->dseg_len);
    serial_write_string(serial, "\n");
}

void write_multiboot_info_framebuffer_palette_color(Serial_t *serial, FramebufferPaletteColor_t *color)
{
    serial_write_string(serial, "Red: ");
    serial_write_int(serial, color->red_value);
    serial_write_string(serial, "\nGreen: ");
    serial_write_int(serial, color->green_value);
    serial_write_string(serial, "\nBlue: ");
    serial_write_int(serial, color->blue_value);
    serial_write_string(serial, "\n");
}

void write_multiboot_info_vbe_info(Serial_t *serial, MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 11)))
        return;
    serial_write_string(serial, "VBE Control Info: ");
    serial_write_hex32(serial, mbi->vbe_control_info);
    serial_write_string(serial, "\nVBE Mode Info: ");
    serial_write_hex32(serial, mbi->vbe_mode_info);
    serial_write_string(serial, "\nVBE Mode: ");
    serial_write_hex32(serial, mbi->vbe_mode);
    serial_write_string(serial, "\nVBE Interface Seg: ");
    serial_write_hex32(serial, mbi->vbe_interface_seg);
    serial_write_string(serial, "\nVBE Interface Off: ");
    serial_write_hex32(serial, mbi->vbe_interface_off);
    serial_write_string(serial, "\nVBE Interface Len: ");
    serial_write_int(serial, mbi->vbe_interface_len);
    serial_write_string(serial, " bytes\n");
}

void write_multiboot_info_framebuffer(Serial_t *serial, MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 12)))
        return;
    serial_write_string(serial, "Framebuffer Address: ");
    serial_write_hex32(serial, (uint32_t) mbi->framebuffer_addr);
    serial_write_string(serial, "\nFramebuffer Pitch: ");
    serial_write_int(serial, mbi->framebuffer_pitch);
    serial_write_string(serial, " bytes\nFramebuffer Width: ");
    serial_write_int(serial, mbi->framebuffer_width);
    serial_write_string(serial, " pixels\nFramebuffer Height: ");
    serial_write_int(serial, mbi->framebuffer_height);
    serial_write_string(serial, " pixels\nFramebuffer BPP: ");
    serial_write_int(serial, mbi->framebuffer_bpp);
    serial_write_string(serial, " bits per pixel\nFramebuffer Type: ");
    serial_write_int(serial, mbi->framebuffer_type);
    const char *fb_type_str = (mbi->framebuffer_type == 0) ? " (Indexed Color)" :
                              (mbi->framebuffer_type == 1) ? " (Direct Color)" :
                              (mbi->framebuffer_type == 2) ? " (EGA Text)" :
                                                             " (Unknown)";
    serial_write_string(serial, fb_type_str);
    serial_write_string(serial, "\n");

    if (mbi->framebuffer_type == 0)
    {
        serial_write_string(serial, "Framebuffer Palette Address: ");
        serial_write_hex32(serial, (uint32_t) (long) mbi->indexed_color_t.framebuffer_palette_addr);
        serial_write_string(serial, "\nNumber of Colors in Palette: ");
        serial_write_int(serial, mbi->indexed_color_t.framebuffer_palette_num_colors);
        serial_write_string(serial, "\n");
    }
    else if (mbi->framebuffer_type == 1)
    {
        serial_write_string(serial, "Red Field Position: ");
        serial_write_int(serial, mbi->direct_color_t.framebuffer_red_field_position);
        serial_write_string(serial, "\nRed Mask Size: ");
        serial_write_int(serial, mbi->direct_color_t.framebuffer_red_mask_size);
        serial_write_string(serial, "\nGreen Field Position: ");
        serial_write_int(serial, mbi->direct_color_t.framebuffer_green_field_position);
        serial_write_string(serial, "\nGreen Mask Size: ");
        serial_write_int(serial, mbi->direct_color_t.framebuffer_green_mask_size);
        serial_write_string(serial, "\nBlue Field Position: ");
        serial_write_int(serial, mbi->direct_color_t.framebuffer_blue_field_position);
        serial_write_string(serial, "\nBlue Mask Size: ");
        serial_write_int(serial, mbi->direct_color_t.framebuffer_blue_mask_size);
        serial_write_string(serial, "\n");
    }
}

void write_multiboot_info(Serial_t *serial, uint32_t kernel_start, MultibootInfo_t *mbi)
{
    serial_write_string(serial, "Flags: ");
    serial_write_hex32(serial, mbi->flags);
    serial_write_string(serial, "\n");

    if (mbi->flags & (1 << 0))
    {
        serial_write_string(serial, "Memory Lower: ");
        serial_write_int(serial, mbi->mem_lower);
        serial_write_string(serial, " KB\nMemory Upper: ");
        serial_write_int(serial, mbi->mem_upper);
        serial_write_string(serial, " KB\n");
    }

    if (mbi->flags & (1 << 1) && mbi->boot_device)
        write_multiboot_info_boot_device(serial, mbi->boot_device);

    if (mbi->flags & (1 << 2))
    {
        const char *s = boot_command_line_source(mbi);
        if (s)
        {
            serial_write_string(serial, "Command line: ");
            serial_write_string(serial, s);
            serial_write_string(serial, "\n");
        }
        else
        {
            serial_write_string(serial, "Command line: (not accessible)\n");
        }
    }

    if (mbi->flags & (1 << 3) && mbi->mods_count > 0 && mbi->mods_addr)
    {
        serial_write_string(serial, "Modules count: ");
        serial_write_int(serial, mbi->mods_count);
        serial_write_string(serial, "\n");
        Module_t *modules = multiboot_modules(mbi, kernel_start);

        if (modules)
        {
            for (uint32_t i = 0; i < mbi->mods_count; i++)
                write_multiboot_info_module(serial, kernel_start, &modules[i]);
        }
        else
        {
            serial_write_string(serial, "Modules unreachable (outside the boot map)\n");
        }
    }

    if (mbi->flags & ((1 << 4) | (1 << 5)))
    {
        if (mbi->flags & (1 << 4))
            write_multiboot_info_aout_symbol_table(serial, &mbi->aout_sym);
        else
            write_multiboot_info_elf_section_header_table(serial, &mbi->elf_sec);
    }

    write_multiboot_info_memory_map(serial, kernel_start, mbi);

    if (mbi->flags & (1 << 7) && mbi->drives_length > 0 && mbi->drives_addr)
    {
        serial_write_string(serial, "Drives Length: ");
        serial_write_int(serial, mbi->drives_length);
        serial_write_string(serial, " bytes\n");
        DriveInfo_t *drive = (DriveInfo_t *) physical_range_boot_mapped((uint32_t) (uintptr_t) mbi->drives_addr,
                                                                        mbi->drives_length, kernel_start);
        uint32_t offset = 0;
        if (!drive)
            serial_write_string(serial, "  Drive info not accessible (physical->virtual failed)\n");
        while (drive && offset < mbi->drives_length && drive->size != 0u)
        {
            write_multiboot_info_drive_info(serial, drive);
            offset += drive->size;
            drive = (DriveInfo_t *) ((uint8_t *) drive + drive->size);
        }
    }
    else
    {
        serial_write_string(serial, "No drive info provided.\n");
    }

    if (mbi->flags & (1 << 8))
    {
        serial_write_string(serial, "Configuration Table Address: ");
        serial_write_hex32(serial, mbi->config_table);
        serial_write_string(serial, "\n");
    }

    if (mbi->flags & (1 << 9))
    {
        const char *name = boot_loader_name_source(mbi);
        serial_write_string(serial, "Boot Loader Name: ");
        serial_write_string(serial, name ? name : "(not accessible)");
        serial_write_string(serial, "\n");
    }

    if (mbi->flags & (1 << 10) && mbi->apm_table)
    {
        APMTable_t *apm_table = (APMTable_t *) physical_range_boot_mapped((uint32_t) (uintptr_t) mbi->apm_table,
                                                                          sizeof(APMTable_t), kernel_start);
        if (apm_table)
            write_multiboot_info_apm_table(serial, apm_table);
        else
            serial_write_string(serial, "APM table not accessible (physical->virtual failed)\n");
    }

    write_multiboot_info_vbe_info(serial, mbi);
    write_multiboot_info_framebuffer(serial, mbi);
}
