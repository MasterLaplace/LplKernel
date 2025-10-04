#include <kernel/multiboot_info_helper.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#    error "This code assumes little-endian (x86). Update multiboot parsing for big-endian targets."
#endif

////////////////////////////////////////////////////////////
// Private functions of the serial module
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
// Public functions of the terminal module API
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
