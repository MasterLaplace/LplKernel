#include <kernel/cpu/ioapic.h>

#include <kernel/cpu/acpi.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pic.h>

#define IOAPIC_MMIO_BASE_VIRT 0xFFB10000u
#define IOAPIC_MMIO_STRIDE    0x1000u

#define IOAPIC_REGISTER_SELECTOR 0x00u
#define IOAPIC_REGISTER_WINDOW   0x10u

#define IOAPIC_REGISTER_ID       0x00u
#define IOAPIC_REGISTER_VERSION  0x01u
#define IOAPIC_REGISTER_REDIRECT 0x10u

#define IOAPIC_REDIR_LOW_VECTOR_MASK         0x000000FFu
#define IOAPIC_REDIR_LOW_POLARITY_ACTIVE_LOW (1u << 13u)
#define IOAPIC_REDIR_LOW_TRIGGER_LEVEL       (1u << 15u)
#define IOAPIC_REDIR_LOW_MASKED              (1u << 16u)

#define ACPI_ISO_POLARITY_MASK        0x0003u
#define ACPI_ISO_POLARITY_ACTIVE_HIGH 0x0001u
#define ACPI_ISO_POLARITY_ACTIVE_LOW  0x0003u
#define ACPI_ISO_TRIGGER_MASK         0x000Cu
#define ACPI_ISO_TRIGGER_EDGE         0x0004u
#define ACPI_ISO_TRIGGER_LEVEL        0x000Cu

static const char *io_apic_state_name = "ioapic-not-initialized";
static uint8_t io_apic_mapped_count = 0u;
static uint8_t io_apic_route_count = 0u;
static InputOutputApicUnit_t io_apic_units[INPUT_OUTPUT_APIC_MAX_COUNT] = {0};
static InputOutputApicRouteInfo_t io_apic_routes[INPUT_OUTPUT_APIC_MAX_ROUTES] = {0};

static inline volatile uint32_t *io_apic_register_pointer(uint32_t io_apic_virtual_base, uint32_t offset)
{
    return (volatile uint32_t *) (io_apic_virtual_base + offset);
}

static uint32_t io_apic_register_read(uint32_t io_apic_virtual_base, uint8_t reg_index)
{
    *io_apic_register_pointer(io_apic_virtual_base, IOAPIC_REGISTER_SELECTOR) = reg_index;
    return *io_apic_register_pointer(io_apic_virtual_base, IOAPIC_REGISTER_WINDOW);
}

static void io_apic_register_write(uint32_t io_apic_virtual_base, uint8_t reg_index, uint32_t value)
{
    *io_apic_register_pointer(io_apic_virtual_base, IOAPIC_REGISTER_SELECTOR) = reg_index;
    *io_apic_register_pointer(io_apic_virtual_base, IOAPIC_REGISTER_WINDOW) = value;
}

static uint8_t io_apic_map_unit(uint8_t index)
{
    uint32_t virt_base;
    PageDirectoryEntry_t pde_flags = {0};
    PageTableEntry_t pte_flags = {0};
    InputOutputApicUnit_t *unit = &io_apic_units[index];

    virt_base = IOAPIC_MMIO_BASE_VIRT + ((uint32_t) index * IOAPIC_MMIO_STRIDE);
    unit->virtual_base = virt_base;

    if (!paging_is_mapped(virt_base))
    {
        pde_flags.present = 1u;
        pde_flags.read_write = 1u;
        pde_flags.user_supervisor = 0u;
        pde_flags.write_through = 0u;
        pde_flags.cache_disable = 1u;

        pte_flags.present = 1u;
        pte_flags.read_write = 1u;
        pte_flags.user_supervisor = 0u;
        pte_flags.write_through = 0u;
        pte_flags.cache_disable = 1u;

        if (!paging_map_page(virt_base, unit->physical_base, pde_flags, pte_flags))
            return 0u;
    }

    unit->mapped = 1u;
    return 1u;
}

static uint32_t io_apic_extract_redirection_count(uint32_t io_apic_virtual_base)
{
    uint32_t version_reg = io_apic_register_read(io_apic_virtual_base, IOAPIC_REGISTER_VERSION);
    uint32_t max_redir_index = (version_reg >> 16u) & 0xFFu;

    return max_redir_index + 1u;
}

static uint8_t io_apic_find_unit_for_gsi(uint32_t gsi, uint8_t *unit_index, uint32_t *gsi_relative)
{
    if (!unit_index || !gsi_relative)
        return 0u;

    for (uint8_t index = 0u; index < INPUT_OUTPUT_APIC_MAX_COUNT; ++index)
    {
        InputOutputApicUnit_t *unit = &io_apic_units[index];
        uint32_t gsi_end;

        if (!unit->mapped)
            continue;

        if (unit->redirection_entry_count == 0u)
            continue;

        gsi_end = unit->gsi_base + unit->redirection_entry_count;
        if (gsi < unit->gsi_base || gsi >= gsi_end)
            continue;

        *unit_index = index;
        *gsi_relative = gsi - unit->gsi_base;
        return 1u;
    }

    return 0u;
}

static uint32_t io_apic_build_redirection_low(uint8_t vector, uint16_t iso_flags)
{
    uint32_t low = 0u;
    uint16_t polarity = iso_flags & ACPI_ISO_POLARITY_MASK;
    uint16_t trigger = iso_flags & ACPI_ISO_TRIGGER_MASK;

    low |= ((uint32_t) vector & IOAPIC_REDIR_LOW_VECTOR_MASK);

    if (polarity == ACPI_ISO_POLARITY_ACTIVE_LOW)
        low |= IOAPIC_REDIR_LOW_POLARITY_ACTIVE_LOW;
    if (polarity == ACPI_ISO_POLARITY_ACTIVE_HIGH)
        low &= ~IOAPIC_REDIR_LOW_POLARITY_ACTIVE_LOW;

    if (trigger == ACPI_ISO_TRIGGER_LEVEL)
        low |= IOAPIC_REDIR_LOW_TRIGGER_LEVEL;
    if (trigger == ACPI_ISO_TRIGGER_EDGE)
        low &= ~IOAPIC_REDIR_LOW_TRIGGER_LEVEL;

    low |= IOAPIC_REDIR_LOW_MASKED;
    return low;
}

static uint8_t io_apic_program_masked_isa_route(uint8_t isa_irq)
{
    uint32_t gsi = 0u;
    uint16_t iso_flags = 0u;
    uint8_t unit_index = 0u;
    uint32_t gsi_relative = 0u;
    uint8_t vector;
    uint32_t redir_low;
    uint32_t redir_high = 0u;
    uint8_t redir_reg_index;
    InputOutputApicUnit_t *unit;
    InputOutputApicRouteInfo_t *route;

    if (io_apic_route_count >= INPUT_OUTPUT_APIC_MAX_ROUTES)
        return 0u;

    if (!advanced_configuration_and_power_interface_madt_resolve_isa_irq(isa_irq, &gsi, &iso_flags))
        return 0u;

    if (!io_apic_find_unit_for_gsi(gsi, &unit_index, &gsi_relative))
        return 0u;

    unit = &io_apic_units[unit_index];
    if (gsi_relative > 0x7Fu)
        return 0u;

    vector = (uint8_t) (PIC_VECTOR_OFFSET_MASTER + isa_irq);
    redir_low = io_apic_build_redirection_low(vector, iso_flags);
    redir_reg_index = (uint8_t) (IOAPIC_REGISTER_REDIRECT + (uint8_t) (gsi_relative * 2u));

    io_apic_register_write(unit->virtual_base, redir_reg_index + 1u, redir_high);
    io_apic_register_write(unit->virtual_base, redir_reg_index, redir_low);

    route = &io_apic_routes[io_apic_route_count];
    route->isa_irq = isa_irq;
    route->gsi = gsi;
    route->vector = vector;
    route->io_apic_index = unit_index;
    route->io_apic_id = unit->id;
    route->masked = 1u;
    route->iso_flags = iso_flags;
    io_apic_route_count++;
    return 1u;
}

static int32_t io_apic_find_programmed_route_index_by_irq(uint8_t isa_irq)
{
    for (uint8_t route_index = 0u; route_index < io_apic_route_count; ++route_index)
    {
        if (io_apic_routes[route_index].isa_irq == isa_irq)
            return (int32_t) route_index;
    }

    return -1;
}

void input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold(void)
{
    uint8_t io_apic_count;

    io_apic_state_name = "ioapic-init-start";
    io_apic_mapped_count = 0u;
    io_apic_route_count = 0u;

    for (uint8_t index = 0u; index < INPUT_OUTPUT_APIC_MAX_COUNT; ++index)
    {
        io_apic_units[index].id = 0u;
        io_apic_units[index].physical_base = 0u;
        io_apic_units[index].virtual_base = 0u;
        io_apic_units[index].gsi_base = 0u;
        io_apic_units[index].redirection_entry_count = 0u;
        io_apic_units[index].mapped = 0u;
    }

    if (!advanced_configuration_and_power_interface_madt_is_available())
    {
        io_apic_state_name = "ioapic-init-acpi-unavailable";
        return;
    }

    io_apic_count = advanced_configuration_and_power_interface_madt_get_io_apic_count();
    if (io_apic_count == 0u)
    {
        io_apic_state_name = "ioapic-init-no-ioapic";
        return;
    }
    if (io_apic_count > INPUT_OUTPUT_APIC_MAX_COUNT)
        io_apic_count = INPUT_OUTPUT_APIC_MAX_COUNT;

    for (uint8_t index = 0u; index < io_apic_count; ++index)
    {
        InputOutputApicUnit_t *unit = &io_apic_units[index];

        unit->id = advanced_configuration_and_power_interface_madt_get_io_apic_id(index);
        unit->physical_base = advanced_configuration_and_power_interface_madt_get_io_apic_physical_base(index);
        unit->gsi_base = advanced_configuration_and_power_interface_madt_get_io_apic_gsi_base(index);

        if (unit->physical_base == 0u)
            continue;
        if (!io_apic_map_unit(index))
        {
            io_apic_state_name = "ioapic-init-mmio-map-failed";
            return;
        }

        unit->redirection_entry_count = io_apic_extract_redirection_count(unit->virtual_base);
        (void) io_apic_register_read(unit->virtual_base, IOAPIC_REGISTER_ID);
        io_apic_mapped_count++;
    }

    if (io_apic_mapped_count == 0u)
    {
        io_apic_state_name = "ioapic-init-no-mapped-units";
        return;
    }

    (void) io_apic_program_masked_isa_route(0u);
    (void) io_apic_program_masked_isa_route(1u);

    if (io_apic_route_count == 0u)
        io_apic_state_name = "ioapic-init-mapped-no-routes";
    else
        io_apic_state_name = "ioapic-init-masked-routes-ready";
}

const char *input_output_advanced_programmable_interrupt_controller_get_state_name(void) { return io_apic_state_name; }

uint8_t input_output_advanced_programmable_interrupt_controller_get_mapped_count(void) { return io_apic_mapped_count; }

uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_count(void)
{
    return io_apic_route_count;
}

uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_irq(uint8_t route_index)
{
    if (route_index >= io_apic_route_count)
        return 0u;
    return io_apic_routes[route_index].isa_irq;
}

uint32_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_gsi(uint8_t route_index)
{
    if (route_index >= io_apic_route_count)
        return 0u;
    return io_apic_routes[route_index].gsi;
}

uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_vector(uint8_t route_index)
{
    if (route_index >= io_apic_route_count)
        return 0u;
    return io_apic_routes[route_index].vector;
}

uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_index(uint8_t route_index)
{
    if (route_index >= io_apic_route_count)
        return 0u;
    return io_apic_routes[route_index].io_apic_index;
}

uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_id(uint8_t route_index)
{
    if (route_index >= io_apic_route_count)
        return 0u;
    return io_apic_routes[route_index].io_apic_id;
}

uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_is_masked(uint8_t route_index)
{
    if (route_index >= io_apic_route_count)
        return 0u;
    return io_apic_routes[route_index].masked;
}

uint16_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_iso_flags(uint8_t route_index)
{
    if (route_index >= io_apic_route_count)
        return 0u;
    return io_apic_routes[route_index].iso_flags;
}

uint8_t input_output_advanced_programmable_interrupt_controller_enable_isa_route(uint8_t isa_irq)
{
    int32_t route_index_signed;
    uint8_t route_index;
    InputOutputApicRouteInfo_t *route;
    InputOutputApicUnit_t *unit;
    uint32_t gsi_relative;
    uint8_t redir_reg_index;
    uint32_t redir_low;

    route_index_signed = io_apic_find_programmed_route_index_by_irq(isa_irq);
    if (route_index_signed < 0)
    {
        io_apic_state_name = "ioapic-route-enable-route-missing";
        return 0u;
    }

    route_index = (uint8_t) route_index_signed;
    route = &io_apic_routes[route_index];

    if (route->io_apic_index >= INPUT_OUTPUT_APIC_MAX_COUNT)
    {
        io_apic_state_name = "ioapic-route-enable-unit-index-invalid";
        return 0u;
    }

    unit = &io_apic_units[route->io_apic_index];
    if (!unit->mapped)
    {
        io_apic_state_name = "ioapic-route-enable-unit-unmapped";
        return 0u;
    }

    if (route->gsi < unit->gsi_base)
    {
        io_apic_state_name = "ioapic-route-enable-gsi-underflow";
        return 0u;
    }

    gsi_relative = route->gsi - unit->gsi_base;
    if (gsi_relative > 0x7Fu)
    {
        io_apic_state_name = "ioapic-route-enable-gsi-relative-invalid";
        return 0u;
    }

    redir_reg_index = (uint8_t) (IOAPIC_REGISTER_REDIRECT + (uint8_t) (gsi_relative * 2u));
    redir_low = io_apic_register_read(unit->virtual_base, redir_reg_index);
    redir_low &= ~IOAPIC_REDIR_LOW_MASKED;
    io_apic_register_write(unit->virtual_base, redir_reg_index, redir_low);

    route->masked = 0u;
    io_apic_state_name = "ioapic-route-enable-active";
    return 1u;
}
