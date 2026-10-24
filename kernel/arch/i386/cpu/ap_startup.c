#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/idt.h>

/**
 * @brief The control registers one application processor read on itself as it entered C.
 */
typedef struct ApplicationProcessorControlRegisters {
    uint32_t control_register_0; /**< CR0 as the trampoline left it. */
    uint32_t control_register_4; /**< CR4 as the trampoline left it. */
    bool recorded;               /**< Whether the processor of this slot reached C and read them. */
} ApplicationProcessorControlRegisters_t;

static uint32_t kernel_cr3_cached = 0u;
static ApplicationProcessorLocalContext_t ap_local_context = {0};
static Serial_t *ap_serial_port = NULL;
static uint32_t ap_startup_reported_online_count = 0u;
static uint32_t ap_startup_last_reported_apic_id = CPU_TOPOLOGY_NO_PROCESSOR;
static bool ap_startup_reported_slots[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC] = {false};
static ApplicationProcessorControlRegisters_t ap_startup_control_registers[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC] = {0};

static bool ap_startup_try_mark_reported_slot(uint32_t logical_slot)
{
    if (logical_slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC || ap_startup_reported_slots[logical_slot])
        return false;
    ap_startup_reported_slots[logical_slot] = true;
    return true;
}

static void ap_startup_record_control_registers(uint32_t logical_slot)
{
    if (logical_slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
        return;

    ap_startup_control_registers[logical_slot] = (ApplicationProcessorControlRegisters_t){
        .control_register_0 = asmutils_read_control_register_0(),
        .control_register_4 = asmutils_read_control_register_4(),
        .recorded = true,
    };
}

static void ap_startup_record_online_event(uint32_t apic_id, uint32_t logical_slot)
{
    cpu_topology_register_discovered_apic_id(apic_id);
    cpu_topology_mark_apic_id_online(apic_id);
    if (ap_startup_try_mark_reported_slot(logical_slot))
    {
        ++ap_startup_reported_online_count;
        ap_startup_last_reported_apic_id = apic_id;
    }
    application_processor_bootstrap_mark_booted(logical_slot);
}

uint32_t application_processor_startup_get_kernel_cr3(void)
{
    if (!kernel_cr3_cached)
        kernel_cr3_cached = (uint32_t) (uintptr_t) &boot_page_directory - KERNEL_VIRTUAL_BASE;

    return kernel_cr3_cached;
}

void application_processor_startup_set_serial_port(Serial_t *serial_port) { ap_serial_port = serial_port; }

uint8_t application_processor_startup_ensure_low_identity_mapping(void)
{
    uint32_t *page_directory = (uint32_t *) (uintptr_t) &boot_page_directory;
    uint32_t page_tables_phys = ((uint32_t) (uintptr_t) &boot_page_tables - KERNEL_VIRTUAL_BASE) & 0xFFFFF000u;

    if ((page_directory[0] & 0x1u) == 0u)
    {
        page_directory[0] = page_tables_phys | 0x003u;
        paging_flush_tlb();
    }

    return (uint8_t) ((page_directory[0] & 0x1u) != 0u);
}

void application_processor_startup_initialize_cpu(uint32_t apic_id, uint32_t logical_slot, void *stack_top)
{
    ap_startup_record_control_registers(logical_slot);

    ap_local_context.apic_id = apic_id;
    ap_local_context.logical_slot = logical_slot;
    ap_local_context.initialized = 1u;

    paging_load_cr3(application_processor_startup_get_kernel_cr3());
    (void) interrupt_descriptor_table_load_on_this_processor();
    apic_initialize_on_cpu(advanced_pic_timer_backend_get_local_apic_virtual_base());

    (void) stack_top;

    ap_startup_record_online_event(apic_id, logical_slot);

    uint32_t domain = cpu_topology_get_slot_domain(logical_slot);

    kernel_heap_initialize_ap_domain(logical_slot);

    if (ap_serial_port)
    {
        serial_write_string(ap_serial_port, "[AP ");
        serial_write_hex32(ap_serial_port, apic_id);
        serial_write_string(ap_serial_port, "]: initialized at slot=");
        serial_write_int(ap_serial_port, (int32_t) logical_slot);
        serial_write_string(ap_serial_port, ", domain=");
        serial_write_int(ap_serial_port, (int32_t) domain);
        serial_write_string(ap_serial_port, "\n");
    }
}

void application_processor_startup_report_bootstrap_ack(uint32_t apic_id, uint32_t logical_slot)
{
    ap_startup_record_online_event(apic_id, logical_slot);
}

uint32_t application_processor_startup_get_reported_online_count(void) { return ap_startup_reported_online_count; }

uint32_t application_processor_startup_get_last_reported_apic_id(void) { return ap_startup_last_reported_apic_id; }

uint32_t application_processor_startup_count_control_register_records(void)
{
    uint32_t recorded = 0u;

    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
        recorded += ap_startup_control_registers[slot].recorded ? 1u : 0u;
    return recorded;
}

uint32_t application_processor_startup_count_control_register_mismatches(void)
{
    const uint32_t control_register_0 = asmutils_read_control_register_0();
    const uint32_t control_register_4 = asmutils_read_control_register_4();
    uint32_t differences = 0u;

    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        const ApplicationProcessorControlRegisters_t *found = &ap_startup_control_registers[slot];

        if (found->recorded &&
            (found->control_register_0 != control_register_0 || found->control_register_4 != control_register_4))
            ++differences;
    }
    return differences;
}

uint32_t application_processor_startup_get_apic_id(void) { return ap_local_context.apic_id; }

uint32_t application_processor_startup_get_logical_slot(void) { return ap_local_context.logical_slot; }

uint8_t application_processor_startup_is_initialized(void) { return ap_local_context.initialized; }

void application_processor_startup_main_loop(void)
{
    asmutils_enable_interrupts();
    while (1)
    {
        asmutils_halt();
    }
}
