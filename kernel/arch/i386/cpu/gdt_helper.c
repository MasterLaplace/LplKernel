#include <kernel/cpu/gdt_helper.h>

////////////////////////////////////////////////////////////
// Private functions of the GDT helper module
////////////////////////////////////////////////////////////

static inline void print_section_header(const char *title, uint8_t color)
{
    terminal_setcolor(vga_entry_color(color, VGA_COLOR_BLACK));
    terminal_write_string("\n--- ");
    terminal_write_string(title);
    terminal_write_string(" ---\n");
}

static inline void print_access_byte_decoded(uint8_t access)
{
    bool present = (access & 0x80) != 0;
    uint8_t dpl = (access >> 5) & 0x03;
    bool descriptor_type = (access & 0x10) != 0;
    bool executable = (access & 0x08) != 0;
    bool direction_conforming = (access & 0x04) != 0;
    bool read_write = (access & 0x02) != 0;
    bool accessed = (access & 0x01) != 0;

    terminal_write_string(present ? "P" : "NP");
    terminal_write_string(" DPL=");
    terminal_write_number(dpl, 10);
    terminal_write_string(" ");
    terminal_write_string(descriptor_type ? (executable ? "Code" : "Data") : "System");
    terminal_write_string(" ");
    terminal_write_string(executable ? (direction_conforming ? "Conforming" : "Non-Conforming") :
                                       (direction_conforming ? "Expand-Down" : "Expand-Up"));
    terminal_write_string(" ");
    terminal_write_string(executable ? (read_write ? "R" : "X") : (read_write ? "W" : "R"));
    terminal_write_string(" ");
    terminal_write_string(accessed ? "A" : "NA");
}

static inline void write_access_byte_decoded(Serial_t *serial, uint8_t access)
{
    bool present = (access & 0x80) != 0;
    uint8_t dpl = (access >> 5) & 0x03;
    bool descriptor_type = (access & 0x10) != 0;
    bool executable = (access & 0x08) != 0;
    bool direction_conforming = (access & 0x04) != 0;
    bool read_write = (access & 0x02) != 0;
    bool accessed = (access & 0x01) != 0;

    serial_write_string(serial, present ? "P" : "NP");
    serial_write_string(serial, " DPL=");
    serial_write_int(serial, dpl);
    serial_write_string(serial, " ");
    serial_write_string(serial, descriptor_type ? (executable ? "Code" : "Data") : "System");
    serial_write_string(serial, " ");
    serial_write_string(serial, executable ? (direction_conforming ? "Conforming" : "Non-Conforming") :
                                             (direction_conforming ? "Expand-Down" : "Expand-Up"));
    serial_write_string(serial, " ");
    serial_write_string(serial, executable ? (read_write ? "R" : "X") : (read_write ? "W" : "R"));
    serial_write_string(serial, " ");
    serial_write_string(serial, accessed ? "A" : "NA");
}

static inline void print_flags_decoded(uint8_t flags)
{
    bool granularity = (flags & 0x80) != 0;
    bool default_big = (flags & 0x40) != 0;
    bool long_mode = (flags & 0x20) != 0;

    terminal_write_string("G=");
    terminal_write_string(granularity ? "4KB" : "1B");
    terminal_write_string(" D/B=");
    terminal_write_string(default_big ? "32-bit" : "16-bit");
    terminal_write_string(" L=");
    terminal_write_string(long_mode ? "Long" : "Compat");
}

static inline void write_flags_decoded(Serial_t *serial, uint8_t flags)
{
    bool granularity = (flags & 0x80) != 0;
    bool default_big = (flags & 0x40) != 0;
    bool long_mode = (flags & 0x20) != 0;

    serial_write_string(serial, "G=");
    serial_write_string(serial, granularity ? "4KB" : "1B");
    serial_write_string(serial, " D/B=");
    serial_write_string(serial, default_big ? "32-bit" : "16-bit");
    serial_write_string(serial, " L=");
    serial_write_string(serial, long_mode ? "Long" : "Compat");
}

static inline void print_gdt_entry(const char *name, const GlobalDescriptorTableEntry_t *entry, uint16_t selector)
{
    terminal_write_string(name);
    terminal_write_string(" (Selector 0x");
    terminal_write_number(selector, 16);
    terminal_write_string("):\n");

    // Reconstruct base address
    uint32_t base = entry->base_low | ((uint32_t)entry->base_middle << 16) | ((uint32_t)entry->base_high << 24);
    terminal_write_string("  Base: 0x");
    terminal_write_number(base, 16);

    // Reconstruct limit
    uint8_t flags_byte = *(uint8_t *)&entry->flags;
    uint32_t limit = entry->limit_low | (((uint32_t)(flags_byte & 0x0F)) << 16);
    terminal_write_string("\n  Limit: 0x");
    terminal_write_number(limit, 16);

    // Decode access byte
    uint8_t access = *(uint8_t *)&entry->access_byte;
    terminal_write_string("\n  Access: 0x");
    terminal_write_number(access, 16);
    terminal_write_string(" (");
    print_access_byte_decoded(access);
    terminal_write_string(")");

    // Decode flags
    terminal_write_string("\n  Flags: 0x");
    terminal_write_number(flags_byte >> 4, 16);
    terminal_write_string(" (");
    print_flags_decoded(flags_byte);
    terminal_write_string(")\n");
}

static inline void write_gdt_entry(Serial_t *serial, const char *name, const GlobalDescriptorTableEntry_t *entry, uint16_t selector)
{
    serial_write_string(serial, name);
    serial_write_string(serial, " (Selector ");
    serial_write_hex16(serial, selector);
    serial_write_string(serial, "):\n");

    // Reconstruct base address
    uint32_t base = entry->base_low | ((uint32_t)entry->base_middle << 16) | ((uint32_t)entry->base_high << 24);
    serial_write_string(serial, "  Base: ");
    serial_write_hex32(serial, base);

    // Reconstruct limit
    uint8_t flags_byte = *(uint8_t *)&entry->flags;
    uint32_t limit = entry->limit_low | (((uint32_t)(flags_byte & 0x0F)) << 16);
    serial_write_string(serial, "\n  Limit: ");
    serial_write_hex32(serial, limit);

    // Decode access byte
    uint8_t access = *(uint8_t *)&entry->access_byte;
    serial_write_string(serial, "\n  Access: ");
    serial_write_hex8(serial, access);
    serial_write_string(serial, " (");
    write_access_byte_decoded(serial, access);
    serial_write_string(serial, ")");

    // Decode flags
    serial_write_string(serial, "\n  Flags: ");
    serial_write_hex8(serial, flags_byte >> 4);
    serial_write_string(serial, " (");
    write_flags_decoded(serial, flags_byte);
    serial_write_string(serial, ")\n");
}

////////////////////////////////////////////////////////////
// Public functions of the GDT helper module API
////////////////////////////////////////////////////////////

void print_global_descriptor_table(GlobalDescriptorTable_t *gdt)
{
    if (!gdt)
        return;

    VGA_COLOR original_color = terminal_getcolor();
    print_section_header("Global Descriptor Table", VGA_COLOR_LIGHT_CYAN);

    terminal_write_string("GDT Address: 0x");
    terminal_write_number((uint32_t)gdt, 16);
    terminal_write_string("\nGDT Size: ");
    terminal_write_number(sizeof(GlobalDescriptorTable_t), 10);
    terminal_write_string(" bytes (");
    terminal_write_number(sizeof(GlobalDescriptorTable_t) / sizeof(GlobalDescriptorTableEntry_t), 10);
    terminal_write_string(" entries)\n\n");

    print_gdt_entry("Null Descriptor", &gdt->null_descriptor, 0x00);
    print_gdt_entry("Kernel Code Segment", &gdt->kernel_mode_code_segment, 0x08);
    print_gdt_entry("Kernel Data Segment", &gdt->kernel_mode_data_segment, 0x10);
    print_gdt_entry("User Code Segment", &gdt->user_mode_code_segment, 0x18);
    print_gdt_entry("User Data Segment", &gdt->user_mode_data_segment, 0x20);
    print_gdt_entry("Task State Segment", &gdt->task_state_segment, 0x28);

    terminal_setcolor(original_color);
}

void write_global_descriptor_table(Serial_t *serial, GlobalDescriptorTable_t *gdt)
{
    if (!serial || !gdt)
        return;

    serial_write_string(serial, "\n=== Global Descriptor Table ===\n");
    serial_write_string(serial, "GDT Address: ");
    serial_write_hex32(serial, (uint32_t)gdt);
    serial_write_string(serial, "\nGDT Size: ");
    serial_write_int(serial, sizeof(GlobalDescriptorTable_t));
    serial_write_string(serial, " bytes (");
    serial_write_int(serial, sizeof(GlobalDescriptorTable_t) / sizeof(GlobalDescriptorTableEntry_t));
    serial_write_string(serial, " entries)\n\n");

    write_gdt_entry(serial, "Null Descriptor", &gdt->null_descriptor, 0x00);
    write_gdt_entry(serial, "Kernel Code Segment", &gdt->kernel_mode_code_segment, 0x08);
    write_gdt_entry(serial, "Kernel Data Segment", &gdt->kernel_mode_data_segment, 0x10);
    write_gdt_entry(serial, "User Code Segment", &gdt->user_mode_code_segment, 0x18);
    write_gdt_entry(serial, "User Data Segment", &gdt->user_mode_data_segment, 0x20);
    write_gdt_entry(serial, "Task State Segment", &gdt->task_state_segment, 0x28);

    serial_write_string(serial, "\n");
}
