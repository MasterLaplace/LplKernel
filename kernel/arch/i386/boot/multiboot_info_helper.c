#include <kernel/boot/multiboot_info_helper.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#    error "This code assumes little-endian (x86). Update multiboot parsing for big-endian targets."
#endif

////////////////////////////////////////////////////////////
// Private functions of the multiboot info helper module
////////////////////////////////////////////////////////////

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

static inline void *physical_to_virtual_low(uint32_t phys_addr, uint32_t kernel_start)
{
    return (phys_addr < 0x100000) ? (void *) (phys_addr + kernel_start) : NULL;
}

static inline void print_multiboot_string(uint32_t phys_addr, uint32_t kernel_start, const char *label)
{
    char *str = (char *) physical_to_virtual_low(phys_addr, kernel_start);

    if (str)
    {
        terminal_write_string(label);
        terminal_write_string(str);
        terminal_write_string("\n");
    }
    else
    {
        terminal_write_string(label);
        terminal_write_string("(not accessible)\n");
    }
}

static inline void print_section_header(const char *title, uint8_t color)
{
    terminal_setcolor(vga_entry_color(color, VGA_COLOR_BLACK));
    terminal_write_string("\n--- ");
    terminal_write_string(title);
    terminal_write_string(" ---\n");
}

////////////////////////////////////////////////////////////
// Public functions of the multiboot info helper module API
////////////////////////////////////////////////////////////

void print_multiboot_info_boot_device(BootDevice_t *boot_device)
{
    print_section_header("Boot Device", VGA_COLOR_LIGHT_GREEN);
    uint32_t device = (uint32_t) (long) boot_device;

    terminal_write_string("Boot device: 0x");
    terminal_write_number(device, 16);

    uint8_t drive = (device >> 24) & 0xFF;
    uint8_t part1 = (device >> 16) & 0xFF;
    uint8_t part2 = (device >> 8) & 0xFF;
    uint8_t part3 = device & 0xFF;

    terminal_write_string(" (Drive: ");
    terminal_write_number(drive, 10);
    terminal_write_string(", Partitions: ");
    terminal_write_number(part1, 10);
    terminal_write_string(", ");
    terminal_write_number(part2, 10);
    terminal_write_string(", ");
    terminal_write_number(part3, 10);
    terminal_write_string(")\n");

    if (drive == 0x80)
    {
        terminal_write_string("  -> First hard disk (0x80)\n");
    }
    else if (drive == 0x00)
    {
        terminal_write_string("  -> First floppy disk (0x00)\n");
    }
    else if (drive >= 0x80)
    {
        terminal_write_string("  -> Hard disk #");
        terminal_write_number(drive - 0x80 + 1, 10);
        terminal_write_string("\n");
    }
    else
    {
        terminal_write_string("  -> Unknown/Virtual device\n");
    }
}

void print_multiboot_info_module(uint32_t kernel_start, Module_t *module)
{
    print_section_header("Module", VGA_COLOR_LIGHT_GREEN);
    terminal_write_string("Module start: 0x");
    terminal_write_number(module->mod_start, 16);
    terminal_write_string("\nModule end: 0x");
    terminal_write_number(module->mod_end, 16);
    terminal_write_string("\nModule string address: 0x");
    terminal_write_number(module->string, 16);
    terminal_write_string("\n");
    print_multiboot_string(module->string, kernel_start, "Module string: ");
}

void print_multiboot_info_aout_symbol_table(AOutSymbolTable_t *aout_sym)
{
    print_section_header("a.out Symbol Table", VGA_COLOR_LIGHT_GREEN);
    terminal_write_string("Table size: ");
    terminal_write_number(aout_sym->tabsize, 10);
    terminal_write_string("\nString size: ");
    terminal_write_number(aout_sym->strsize, 10);
    terminal_write_string("\nAddress: 0x");
    terminal_write_number(aout_sym->addr, 16);
    terminal_write_string("\n");
}

void print_multiboot_info_elf_section_header_table(ELFSectionHeaderTable_t *elf_sec)
{
    print_section_header("ELF Section Header Table", VGA_COLOR_LIGHT_GREEN);
    terminal_write_string("Number of entries: ");
    terminal_write_number(elf_sec->num, 10);
    terminal_write_string("\nSize of each entry: ");
    terminal_write_number(elf_sec->size, 10);
    terminal_write_string("\nAddress: 0x");
    terminal_write_number(elf_sec->addr, 16);
    terminal_write_string("\nString table index: ");
    terminal_write_number(elf_sec->shndx, 10);
    terminal_write_string("\n");
}

void print_multiboot_info_memory_map(uint32_t kernel_start, MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 6) && mbi->mmap_length > 0 && mbi->mmap_addr))
    {
        terminal_write_string("No memory map provided.\n");
        return;
    }

    print_section_header("Memory Map", VGA_COLOR_LIGHT_GREEN);

    terminal_write_string("Memory Map Length: ");
    terminal_write_number(mbi->mmap_length, 10);
    terminal_write_string(" bytes\n");
    terminal_write_string("Memory Map Address: 0x");
    terminal_write_number((uint32_t) (long) mbi->mmap_addr, 16);
    terminal_write_string("\n");

    /* Convert physical mmap address to virtual using kernel_start mapping */
    uint8_t *mmap_base = (uint8_t *) physical_to_virtual_low((uint32_t) (long) mbi->mmap_addr, kernel_start);
    if (!mmap_base)
    {
        terminal_write_string("  Memory map not accessible (physical->virtual failed)\n");
    }
    else
    {
        uint32_t offset = 0;
        uint32_t index = 0;

        while (offset < mbi->mmap_length)
        {
            uint8_t *entry = mmap_base + offset;

            /* ensure we can read the size field */
            if (offset + 4 > mbi->mmap_length)
                break;
            uint32_t size = read_little_endian_u32(entry);

            /* size is the payload length (following the u32 size). The
             * standard payload for a BIOS memory map entry is 20 bytes
             * (8 base + 8 length + 4 type). Guard against malformed data. */
            if (size < 20)
                break;
            if (offset + 4 + size > mbi->mmap_length)
                break;

            uint8_t *payload = entry + 4;
            uint64_t base_addr = read_little_endian_u64(payload);
            uint64_t length = read_little_endian_u64(payload + 8);
            uint32_t type = read_little_endian_u32(payload + 16);

            terminal_write_string("Memory Map Entry ");
            terminal_write_number(index, 10);
            terminal_write_string(":\n");
            terminal_write_string("  Base Address: 0x");
            terminal_write_number(base_addr, 16);
            terminal_write_string("\n  Length: 0x");
            terminal_write_number(length, 16);
            terminal_write_string("\n  Type: ");
            terminal_write_number(type, 10);
            const char *mem_type_desc = (type == 1) ? " (Available)" :
                                        (type == 2) ? " (Reserved)" :
                                        (type == 3) ? " (ACPI Reclaimable)" :
                                        (type == 4) ? " (ACPI NVS)" :
                                        (type == 5) ? " (Bad RAM)" :
                                                      " (Unknown/Reserved)";
            terminal_write_string(mem_type_desc);
            terminal_write_string("\n");

            offset += 4 + size;
            ++index;
        }
    }
}

void print_multiboot_info_drive_info(DriveInfo_t *drive_info)
{
    print_section_header("Drive Info", VGA_COLOR_LIGHT_GREEN);
    terminal_write_string("Structure size: ");
    terminal_write_number(drive_info->size, 10);
    terminal_write_string("\nDrive number: ");
    terminal_write_number(drive_info->drive_number, 10);
    terminal_write_string("\nDrive mode: ");
    terminal_write_number(drive_info->drive_mode, 10);

    const char *mode_str = (drive_info->drive_mode == 0) ? " (CHS)" :
                           (drive_info->drive_mode == 1) ? " (LBA)" :
                                                           " (Unknown)";
    terminal_write_string(mode_str);

    terminal_write_string("\nCylinders: ");
    terminal_write_number(drive_info->drive_cylinders, 10);
    terminal_write_string("\nHeads: ");
    terminal_write_number(drive_info->drive_heads, 10);
    terminal_write_string("\nSectors: ");
    terminal_write_number(drive_info->drive_sectors, 10);

    /* drive_ports is a packed flexible array located immediately after the
     * fixed header. We must read it byte-wise to avoid unaligned accesses.
     * The array is terminated by a 16-bit zero value. */
    terminal_write_string("\nI/O Ports: ");
    {
        uint8_t *base = (uint8_t *) drive_info;
        uint8_t *ptr = base + offsetof(DriveInfo_t, drive_ports);
        for (;;)
        {
            uint16_t port = read_little_endian_u16(ptr);
            if (port == 0)
                break;
            terminal_write_string("0x");
            terminal_write_number(port, 16);
            terminal_write_string(" ");
            ptr += 2;
        }
    }
    terminal_write_string("\n");
}

void print_multiboot_info_apm_table(APMTable_t *apm_table)
{
    print_section_header("APM Table", VGA_COLOR_LIGHT_GREEN);
    terminal_write_string("Version: ");
    terminal_write_number(apm_table->version, 10);
    terminal_write_string("\nCSEG: 0x");
    terminal_write_number(apm_table->cseg, 16);
    terminal_write_string("\nOffset: 0x");
    terminal_write_number(apm_table->offset, 16);
    terminal_write_string("\nCSEG 16: 0x");
    terminal_write_number(apm_table->cseg_16, 16);
    terminal_write_string("\nDSEG: 0x");
    terminal_write_number(apm_table->dseg, 16);
    terminal_write_string("\nFlags: 0x");
    terminal_write_number(apm_table->flags, 16);
    terminal_write_string("\nCSEG Length: ");
    terminal_write_number(apm_table->cseg_len, 10);
    terminal_write_string("\nCSEG 16 Length: ");
    terminal_write_number(apm_table->cseg_16_len, 10);
    terminal_write_string("\nDSEG Length: ");
    terminal_write_number(apm_table->dseg_len, 10);
    terminal_write_string("\n");
}

void print_multiboot_info_framebuffer_palette_color(FramebufferPaletteColor_t *color)
{
    print_section_header("Framebuffer Palette Color", VGA_COLOR_LIGHT_GREEN);
    terminal_write_string("Red: ");
    terminal_write_number(color->red_value, 10);
    terminal_write_string("\nGreen: ");
    terminal_write_number(color->green_value, 10);
    terminal_write_string("\nBlue: ");
    terminal_write_number(color->blue_value, 10);
    terminal_write_string("\n");
}

void print_multiboot_info_vbe_info(MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 11)))
        return;

    print_section_header("VBE Info", VGA_COLOR_LIGHT_GREEN);
    terminal_write_string("VBE Control Info: 0x");
    terminal_write_number(mbi->vbe_control_info, 16);
    terminal_write_string("\nVBE Mode Info: 0x");
    terminal_write_number(mbi->vbe_mode_info, 16);
    terminal_write_string("\nVBE Mode: 0x");
    terminal_write_number(mbi->vbe_mode, 16);
    terminal_write_string("\nVBE Interface Seg: 0x");
    terminal_write_number(mbi->vbe_interface_seg, 16);
    terminal_write_string("\nVBE Interface Off: 0x");
    terminal_write_number(mbi->vbe_interface_off, 16);
    terminal_write_string("\nVBE Interface Len: ");
    terminal_write_number(mbi->vbe_interface_len, 10);
    terminal_write_string(" bytes\n");
}

void print_multiboot_info_framebuffer(MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 12)))
        return;

    print_section_header("Framebuffer Info", VGA_COLOR_LIGHT_GREEN);
    terminal_write_string("Framebuffer Address: 0x");
    terminal_write_number((uint32_t) mbi->framebuffer_addr, 16);
    terminal_write_string("\nFramebuffer Pitch: ");
    terminal_write_number(mbi->framebuffer_pitch, 10);
    terminal_write_string(" bytes\nFramebuffer Width: ");
    terminal_write_number(mbi->framebuffer_width, 10);
    terminal_write_string(" pixels\nFramebuffer Height: ");
    terminal_write_number(mbi->framebuffer_height, 10);
    terminal_write_string(" pixels\nFramebuffer BPP: ");
    terminal_write_number(mbi->framebuffer_bpp, 10);
    terminal_write_string(" bits per pixel\nFramebuffer Type: ");
    terminal_write_number(mbi->framebuffer_type, 10);
    const char *fb_type_str = (mbi->framebuffer_type == 0) ? " (Indexed Color)" :
                              (mbi->framebuffer_type == 1) ? " (Direct Color)" :
                              (mbi->framebuffer_type == 2) ? " (EGA Text)" :
                                                             " (Unknown)";
    terminal_write_string(fb_type_str);
    terminal_write_string("\n");

    if (mbi->framebuffer_type == 0)
    {
        terminal_write_string("Framebuffer Palette Address: 0x");
        terminal_write_number((uint32_t) (long) mbi->indexed_color_t.framebuffer_palette_addr, 16);
        terminal_write_string("\nNumber of Colors in Palette: ");
        terminal_write_number(mbi->indexed_color_t.framebuffer_palette_num_colors, 10);
        terminal_write_string("\n");
    }
    else if (mbi->framebuffer_type == 1)
    {
        terminal_write_string("Red Field Position: ");
        terminal_write_number(mbi->direct_color_t.framebuffer_red_field_position, 10);
        terminal_write_string("\nRed Mask Size: ");
        terminal_write_number(mbi->direct_color_t.framebuffer_red_mask_size, 10);
        terminal_write_string("\nGreen Field Position: ");
        terminal_write_number(mbi->direct_color_t.framebuffer_green_field_position, 10);
        terminal_write_string("\nGreen Mask Size: ");
        terminal_write_number(mbi->direct_color_t.framebuffer_green_mask_size, 10);
        terminal_write_string("\nBlue Field Position: ");
        terminal_write_number(mbi->direct_color_t.framebuffer_blue_field_position, 10);
        terminal_write_string("\nBlue Mask Size: ");
        terminal_write_number(mbi->direct_color_t.framebuffer_blue_mask_size, 10);
        terminal_write_string("\n");
    }
}

void print_multiboot_info(uint32_t kernel_start, MultibootInfo_t *mbi)
{
    VGA_COLOR original_color = terminal_getcolor();
    print_section_header("Multiboot Info", VGA_COLOR_LIGHT_GREEN);

    terminal_write_string("Flags: 0x");
    terminal_write_number(mbi->flags, 16);
    terminal_write_string("\n");

    if (mbi->flags & (1 << 0))
    {
        terminal_write_string("Memory Lower: ");
        terminal_write_number(mbi->mem_lower, 10);
        terminal_write_string(" KB\nMemory Upper: ");
        terminal_write_number(mbi->mem_upper, 10);
        terminal_write_string(" KB\n");
    }

    if (mbi->flags & (1 << 1) && mbi->boot_device)
    {
        print_multiboot_info_boot_device(mbi->boot_device);
    }

    if (mbi->flags & (1 << 2))
    {
        print_multiboot_string(mbi->cmdline, kernel_start, "Command line: ");
    }

    if (mbi->flags & (1 << 3) && mbi->mods_count > 0 && mbi->mods_addr)
    {
        terminal_write_string("Modules count: ");
        terminal_write_number(mbi->mods_count, 10);
        terminal_write_string("\n");

        for (uint32_t i = 0; i < mbi->mods_count; i++)
        {
            print_multiboot_info_module(kernel_start, &mbi->mods_addr[i]);
        }
    }

    if (mbi->flags & ((1 << 4) | (1 << 5)))
    {
        if (mbi->flags & (1 << 4))
        {
            print_multiboot_info_aout_symbol_table(&mbi->aout_sym);
        }
        else
        {
            print_multiboot_info_elf_section_header_table(&mbi->elf_sec);
        }
    }

    print_multiboot_info_memory_map(kernel_start, mbi);

    if (mbi->flags & (1 << 7) && mbi->drives_length > 0 && mbi->drives_addr)
    {
        terminal_write_string("Drives Length: ");
        terminal_write_number(mbi->drives_length, 10);
        terminal_write_string(" bytes\n");
        terminal_write_string("Drives Address: 0x");
        terminal_write_number((uint32_t) (long) mbi->drives_addr, 16);
        terminal_write_string("\n");

        DriveInfo_t *drive = mbi->drives_addr;
        uint32_t offset = 0;

        while (offset < mbi->drives_length)
        {
            print_multiboot_info_drive_info(drive);
            offset += drive->size;
            drive = (DriveInfo_t *) ((uint8_t *) drive + drive->size);
        }
    }
    else
    {
        terminal_write_string("No drive info provided.\n");
    }

    if (mbi->flags & (1 << 8))
    {
        terminal_write_string("Configuration Table Address: 0x");
        terminal_write_number(mbi->config_table, 16);
        terminal_write_string("\n");
    }

    if (mbi->flags & (1 << 9))
    {
        print_multiboot_string(mbi->boot_loader_name, kernel_start, "Boot Loader Name: ");
    }

    if (mbi->flags & (1 << 10) && mbi->apm_table)
    {
        print_multiboot_info_apm_table(mbi->apm_table);
    }

    print_multiboot_info_vbe_info(mbi);

    print_multiboot_info_framebuffer(mbi);

    terminal_setcolor(original_color);
}

//********************************************************//

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
    char *s = (char *) physical_to_virtual_low(module->string, kernel_start);

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

    uint8_t *mmap_base = (uint8_t *) physical_to_virtual_low((uint32_t) (long) mbi->mmap_addr, kernel_start);
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
        char *s = (char *) physical_to_virtual_low(mbi->cmdline, kernel_start);
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
        for (uint32_t i = 0; i < mbi->mods_count; i++)
            write_multiboot_info_module(serial, kernel_start, &mbi->mods_addr[i]);
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
        DriveInfo_t *drive = mbi->drives_addr;
        uint32_t offset = 0;
        while (offset < mbi->drives_length)
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
        char *s = (char *) physical_to_virtual_low(mbi->boot_loader_name, kernel_start);
        if (s)
        {
            serial_write_string(serial, "Boot Loader Name: ");
            serial_write_string(serial, s);
            serial_write_string(serial, "\n");
        }
    }

    if (mbi->flags & (1 << 10) && mbi->apm_table)
        write_multiboot_info_apm_table(serial, mbi->apm_table);

    write_multiboot_info_vbe_info(serial, mbi);
    write_multiboot_info_framebuffer(serial, mbi);
}
