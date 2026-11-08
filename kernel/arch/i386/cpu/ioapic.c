#include <kernel/acpi/interrupt_routing.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/isr.h>

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

/** Destination APIC id field of a redirection entry: bits 24..31 of its high register. */
#define IOAPIC_REDIR_HIGH_DESTINATION_MASK  0xFF000000u
#define IOAPIC_REDIR_HIGH_DESTINATION_SHIFT 24u /**< Where the destination field starts in the high register. */
#define IOAPIC_HIGHEST_PHYSICAL_DESTINATION                                                                            \
    0xFEu /**< The highest APIC ID a physical destination names: 0xFF broadcasts (SDM 325384-093US §13.6.2.1). */

#define ACPI_ISO_POLARITY_MASK        0x0003u
#define ACPI_ISO_POLARITY_ACTIVE_HIGH 0x0001u
#define ACPI_ISO_POLARITY_ACTIVE_LOW  0x0003u
#define ACPI_ISO_TRIGGER_MASK         0x000Cu
#define ACPI_ISO_TRIGGER_EDGE         0x0004u
#define ACPI_ISO_TRIGGER_LEVEL        0x000Cu

#define IOAPIC_PCI_INTERRUPT_PIN 0x3Du /**< A PCI function's Interrupt Pin register: 0 none, 1 to 4 INTA to INTD. */
#define IOAPIC_PCI_PINS          4u    /**< INTA to INTD. */
#define IOAPIC_LAST_REACHABLE_ENTRY                                                                                    \
    0x77u /**< The last entry an I/O APIC's 8-bit register index reaches: entry n takes registers 0x10 + 2n and        \
               0x11 + 2n (Intel 82093AA datasheet, §3.2.4), and 0x78 would wrap to 0x00. */
#define IOAPIC_ISA_IRQS 16u /**< ISA IRQs 0 to 15, the lines of the 8259 pair. */

static const char *io_apic_state_name = "ioapic-not-initialized";
static uint8_t io_apic_mapped_count = 0u;
static uint8_t io_apic_route_count = 0u;
static uint32_t io_apic_lines_refused = 0u;
static InputOutputApicUnit_t io_apic_units[INPUT_OUTPUT_APIC_MAX_COUNT] = {0};
static InputOutputApicRouteInfo_t io_apic_routes[INPUT_OUTPUT_APIC_MAX_ROUTES] = {0};

/**
 * @brief A line a PCI function's pin holds: its Global System Interrupt and the vector it carries.
 */
typedef struct {
    uint32_t line;  /**< The Global System Interrupt. */
    uint8_t vector; /**< The vector it carries. */
} InputOutputApicDeviceLine_t;

/** The lines routed to PCI functions. */
static InputOutputApicDeviceLine_t
    io_apic_device_lines[INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_MAX_DEVICE_LINES];
static uint32_t io_apic_device_line_count = 0u; /**< How many io_apic_device_lines holds. */

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

static uint8_t io_apic_find_unit_for_gsi(uint32_t global_system_interrupt, uint8_t *unit_index, uint32_t *gsi_relative)
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
        if (global_system_interrupt < unit->gsi_base || global_system_interrupt >= gsi_end)
            continue;

        *unit_index = index;
        *gsi_relative = global_system_interrupt - unit->gsi_base;
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

static uint8_t io_apic_can_name_destination(uint32_t apic_id)
{
    return (apic_id <= IOAPIC_HIGHEST_PHYSICAL_DESTINATION) ? 1u : 0u;
}

static uint8_t io_apic_program_masked_isa_route(uint8_t isa_irq, uint32_t destination)
{
    uint32_t global_system_interrupt = 0u;
    uint16_t iso_flags = 0u;
    uint8_t unit_index = 0u;
    uint32_t gsi_relative = 0u;
    uint8_t vector;
    uint32_t redir_low;
    uint32_t redir_high;
    uint8_t redir_reg_index;
    InputOutputApicUnit_t *unit;
    InputOutputApicRouteInfo_t *route;

    if (io_apic_route_count >= INPUT_OUTPUT_APIC_MAX_ROUTES)
        return 0u;

    if (!advanced_configuration_and_power_interface_madt_resolve_isa_irq(isa_irq, &global_system_interrupt, &iso_flags))
        return 0u;

    if (!io_apic_find_unit_for_gsi(global_system_interrupt, &unit_index, &gsi_relative))
        return 0u;

    unit = &io_apic_units[unit_index];
    if (!input_output_advanced_programmable_interrupt_controller_redirection_register(gsi_relative, &redir_reg_index))
        return 0u;

    vector = (uint8_t) (PIC_VECTOR_OFFSET_MASTER + isa_irq);
    redir_low = io_apic_build_redirection_low(vector, iso_flags);

    if (!io_apic_can_name_destination(destination))
    {
        io_apic_register_write(unit->virtual_base, redir_reg_index, redir_low);
        io_apic_lines_refused++;
        return 0u;
    }

    redir_high = destination << IOAPIC_REDIR_HIGH_DESTINATION_SHIFT;
    io_apic_register_write(unit->virtual_base, redir_reg_index + 1u, redir_high);
    io_apic_register_write(unit->virtual_base, redir_reg_index, redir_low);

    route = &io_apic_routes[io_apic_route_count];
    route->isa_irq = isa_irq;
    route->global_system_interrupt = global_system_interrupt;
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

static const char *io_apic_routing_state_name(void)
{
    if (io_apic_route_count > 0u)
        return "ioapic-init-masked-routes-ready";
    if (io_apic_lines_refused > 0u)
        return "ioapic-init-bootstrap-processor-unnameable";
    return "ioapic-init-mapped-no-routes";
}

static uint8_t io_apic_read_route_destination(const InputOutputApicRouteInfo_t *route)
{
    const InputOutputApicUnit_t *unit = &io_apic_units[route->io_apic_index];
    const uint32_t gsi_relative = route->global_system_interrupt - unit->gsi_base;
    const uint8_t redir_reg_index = (uint8_t) (IOAPIC_REGISTER_REDIRECT + (uint8_t) (gsi_relative * 2u));
    const uint32_t redir_high = io_apic_register_read(unit->virtual_base, redir_reg_index + 1u);

    return (uint8_t) ((redir_high & IOAPIC_REDIR_HIGH_DESTINATION_MASK) >> IOAPIC_REDIR_HIGH_DESTINATION_SHIFT);
}

/**
 * @brief The register index of the low half of the redirection entry of @p line, and its unit, when a mapped I/O APIC
 *        holds it.
 */
static bool io_apic_entry_of_line(uint32_t line, InputOutputApicUnit_t **out_unit, uint8_t *out_register)
{
    uint8_t unit_index = 0u;
    uint32_t relative = 0u;

    if (!io_apic_find_unit_for_gsi(line, &unit_index, &relative) ||
        !input_output_advanced_programmable_interrupt_controller_redirection_register(relative, out_register))
        return false;
    *out_unit = &io_apic_units[unit_index];
    return true;
}

/**
 * @brief Whether an ISA IRQ whose 8259 vector has a handler reaches @p line, as the MADT maps ISA IRQs to Global System
 *        Interrupts: its device drives that I/O APIC pin too, edge-triggered.
 */
static bool io_apic_line_serves_an_isa_handler(uint32_t line)
{
    for (uint8_t isa_irq = 0u; isa_irq < IOAPIC_ISA_IRQS; ++isa_irq)
    {
        uint32_t global_system_interrupt = 0u;
        uint16_t flags = 0u;

        if (advanced_configuration_and_power_interface_madt_resolve_isa_irq(isa_irq, &global_system_interrupt,
                                                                            &flags) &&
            global_system_interrupt == line &&
            interrupt_service_routine_get_handler((uint8_t) (PIC_VECTOR_OFFSET_MASTER + isa_irq)) != NULL)
            return true;
    }
    return false;
}

/**
 * @brief Whether an ISA route holds @p line, an ISA IRQ the 8259 serves reaches it, or another function's line carries
 *        another vector on it; a line the same vector holds is taken again.
 */
static bool io_apic_line_is_taken(uint32_t line, uint8_t vector)
{
    for (uint8_t route = 0u; route < io_apic_route_count; ++route)
        if (io_apic_routes[route].global_system_interrupt == line)
            return true;
    if (io_apic_line_serves_an_isa_handler(line))
        return true;
    for (uint32_t index = 0u; index < io_apic_device_line_count; ++index)
        if (io_apic_device_lines[index].line == line)
            return io_apic_device_lines[index].vector != vector;
    return false;
}

/**
 * @brief Records that @p line carries @p vector; false when the lines are all taken.
 */
static bool io_apic_record_device_line(uint32_t line, uint8_t vector)
{
    for (uint32_t index = 0u; index < io_apic_device_line_count; ++index)
        if (io_apic_device_lines[index].line == line)
            return true;
    if (io_apic_device_line_count == INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_MAX_DEVICE_LINES)
        return false;
    io_apic_device_lines[io_apic_device_line_count].line = line;
    io_apic_device_lines[io_apic_device_line_count].vector = vector;
    ++io_apic_device_line_count;
    return true;
}

/**
 * @brief The route the boot published for the interrupt pin of @p device, when the APIC model was told and the
 *        function has a pin.
 */
static bool io_apic_device_route(const PeripheralComponentInterconnectDevice_t *device,
                                 AdvancedConfigurationAndPowerInterfaceInterruptRoute_t *out_route)
{
    const AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *routing =
        advanced_configuration_and_power_interface_interrupt_routing();
    const uint8_t pin = peripheral_component_interconnect_config_read_byte(device->bus, device->device,
                                                                           device->function, IOAPIC_PCI_INTERRUPT_PIN);

    return routing->model == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_APIC && pin != 0u &&
           pin <= IOAPIC_PCI_PINS &&
           advanced_configuration_and_power_interface_interrupt_routing_find(routing, 0u, device->bus, device->device,
                                                                             (uint8_t) (pin - 1u), out_route);
}

void input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold(void)
{
    const uint32_t bootstrap_processor_apic_id = cpu_topology_get_local_apic_id();
    uint8_t io_apic_count;

    io_apic_state_name = "ioapic-init-start";
    io_apic_mapped_count = 0u;
    io_apic_route_count = 0u;
    io_apic_lines_refused = 0u;
    io_apic_device_line_count = 0u;

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
        const uint64_t physical_base = advanced_configuration_and_power_interface_madt_get_io_apic_physical_base(index);

        if (physical_base > UINT32_MAX)
        {
            io_apic_state_name = "ioapic-init-base-above-4gib";
            return;
        }
        unit->id = advanced_configuration_and_power_interface_madt_get_io_apic_id(index);
        unit->physical_base = (uint32_t) physical_base;
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

    (void) io_apic_program_masked_isa_route(0u, bootstrap_processor_apic_id);
    (void) io_apic_program_masked_isa_route(1u, bootstrap_processor_apic_id);

    io_apic_state_name = io_apic_routing_state_name();
}

const char *input_output_advanced_programmable_interrupt_controller_get_state_name(void) { return io_apic_state_name; }

uint8_t input_output_advanced_programmable_interrupt_controller_get_mapped_count(void) { return io_apic_mapped_count; }

uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_count(void)
{
    return io_apic_route_count;
}

uint32_t input_output_advanced_programmable_interrupt_controller_get_lines_refused(void)
{
    return io_apic_lines_refused;
}

uint8_t input_output_advanced_programmable_interrupt_controller_debug_read_line_mask(uint8_t isa_irq, uint8_t *masked)
{
    uint32_t global_system_interrupt = 0u;
    uint16_t iso_flags = 0u;
    uint8_t unit_index = 0u;
    uint32_t gsi_relative = 0u;
    uint8_t redir_reg_index;
    uint32_t redir_low;

    if (!masked)
        return 0u;
    if (!advanced_configuration_and_power_interface_madt_resolve_isa_irq(isa_irq, &global_system_interrupt, &iso_flags))
        return 0u;
    if (!io_apic_find_unit_for_gsi(global_system_interrupt, &unit_index, &gsi_relative) ||
        !input_output_advanced_programmable_interrupt_controller_redirection_register(gsi_relative, &redir_reg_index))
        return 0u;

    redir_low = io_apic_register_read(io_apic_units[unit_index].virtual_base, redir_reg_index);
    *masked = (redir_low & IOAPIC_REDIR_LOW_MASKED) ? 1u : 0u;
    return 1u;
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
    return io_apic_routes[route_index].global_system_interrupt;
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

    if (route->global_system_interrupt < unit->gsi_base)
    {
        io_apic_state_name = "ioapic-route-enable-global_system_interrupt-underflow";
        return 0u;
    }

    gsi_relative = route->global_system_interrupt - unit->gsi_base;
    if (!input_output_advanced_programmable_interrupt_controller_redirection_register(gsi_relative, &redir_reg_index))
    {
        io_apic_state_name = "ioapic-route-enable-global_system_interrupt-relative-invalid";
        return 0u;
    }

    redir_low = io_apic_register_read(unit->virtual_base, redir_reg_index);
    redir_low &= ~IOAPIC_REDIR_LOW_MASKED;
    io_apic_register_write(unit->virtual_base, redir_reg_index, redir_low);

    route->masked = 0u;
    io_apic_state_name = "ioapic-route-enable-active";
    return 1u;
}

uint8_t
input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(uint8_t route_index)
{
    if (route_index >= io_apic_route_count)
        return 0u;
    return io_apic_read_route_destination(&io_apic_routes[route_index]);
}

uint8_t input_output_advanced_programmable_interrupt_controller_set_isa_route_destination(uint8_t isa_irq,
                                                                                          uint32_t apic_id)
{
    int32_t route_index_signed;
    uint8_t route_index;
    InputOutputApicRouteInfo_t *route;
    InputOutputApicUnit_t *unit;
    uint32_t gsi_relative;
    uint8_t redir_reg_index;
    uint32_t redir_high;

    route_index_signed = io_apic_find_programmed_route_index_by_irq(isa_irq);
    if (route_index_signed < 0)
        return 0u;

    route_index = (uint8_t) route_index_signed;
    route = &io_apic_routes[route_index];

    if (route->io_apic_index >= INPUT_OUTPUT_APIC_MAX_COUNT)
        return 0u;

    unit = &io_apic_units[route->io_apic_index];
    if (!unit->mapped)
        return 0u;

    if (route->global_system_interrupt < unit->gsi_base)
        return 0u;

    gsi_relative = route->global_system_interrupt - unit->gsi_base;
    if (!input_output_advanced_programmable_interrupt_controller_redirection_register(gsi_relative, &redir_reg_index))
        return 0u;

    if (!io_apic_can_name_destination(apic_id))
        return 0u;

    redir_high = io_apic_register_read(unit->virtual_base, redir_reg_index + 1u);
    redir_high &= ~IOAPIC_REDIR_HIGH_DESTINATION_MASK;
    redir_high |= apic_id << IOAPIC_REDIR_HIGH_DESTINATION_SHIFT;
    io_apic_register_write(unit->virtual_base, redir_reg_index + 1u, redir_high);

    return 1u;
}

bool input_output_advanced_programmable_interrupt_controller_redirection_register(uint32_t entry, uint8_t *out_register)
{
    if (entry > IOAPIC_LAST_REACHABLE_ENTRY)
        return false;
    *out_register = (uint8_t) (IOAPIC_REGISTER_REDIRECT + entry * 2u);
    return true;
}

bool input_output_advanced_programmable_interrupt_controller_route_device(
    const PeripheralComponentInterconnectDevice_t *device, uint32_t slot, uint8_t vector, uint32_t *out_line)
{
    AdvancedConfigurationAndPowerInterfaceInterruptRoute_t route;
    InputOutputApicUnit_t *unit = NULL;
    uint8_t entry = 0u;
    const uint32_t apic_id = cpu_topology_get_apic_id_at_slot(slot);

    *out_line = INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_NO_LINE;
    if (!io_apic_device_route(device, &route) || !io_apic_entry_of_line(route.interrupt, &unit, &entry) ||
        io_apic_line_is_taken(route.interrupt, vector) || apic_id == CPU_TOPOLOGY_NO_PROCESSOR ||
        !io_apic_can_name_destination(apic_id) || !device_interrupt_platform_delivers() ||
        !io_apic_record_device_line(route.interrupt, vector))
        return false;

    const uint32_t low = (uint32_t) vector | (route.level ? IOAPIC_REDIR_LOW_TRIGGER_LEVEL : 0u) |
                         (route.active_low ? IOAPIC_REDIR_LOW_POLARITY_ACTIVE_LOW : 0u);

    io_apic_register_write(unit->virtual_base, entry, low | IOAPIC_REDIR_LOW_MASKED);
    io_apic_register_write(unit->virtual_base, entry + 1u, apic_id << IOAPIC_REDIR_HIGH_DESTINATION_SHIFT);
    io_apic_register_write(unit->virtual_base, entry, low);
    *out_line = route.interrupt;
    return true;
}

bool input_output_advanced_programmable_interrupt_controller_debug_read_line(uint32_t line, uint32_t *out_low,
                                                                             uint32_t *out_high)
{
    InputOutputApicUnit_t *unit = NULL;
    uint8_t entry = 0u;

    if (!io_apic_entry_of_line(line, &unit, &entry))
        return false;
    *out_low = io_apic_register_read(unit->virtual_base, entry);
    *out_high = io_apic_register_read(unit->virtual_base, entry + 1u);
    return true;
}
