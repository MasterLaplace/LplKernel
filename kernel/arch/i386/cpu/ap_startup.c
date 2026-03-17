/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** AP (Application Processor) entry point and CPU initialization
*/

#include <kernel/cpu/ap_bootstrap.h>
#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/gdt.h>
#include <kernel/cpu/idt.h>
#include <kernel/cpu/paging.h>
#include <kernel/mm/heap.h>
#include <stddef.h>
#include <stdint.h>

/* Kernel CR3 value (page directory physical address) */
extern void *boot_page_directory;  /* Defined in boot.S */
extern void *boot_page_tables;     /* Defined in boot.S */
extern InterruptDescriptorTable_t interrupt_descriptor_table;

/* Higher-half base (see paging.h) */

static uint32_t kernel_cr3_cached = 0u;
static ApplicationProcessorLocalContext_t ap_local_context = { 0 };
static Serial_t *ap_serial_port = NULL;  /* Shared serial port reference */
static uint32_t ap_startup_reported_online_count = 0u;
static uint8_t ap_startup_last_reported_apic_id = 0xFFu;
static uint8_t ap_startup_reported_apic_bitmap[32] = { 0u };

static uint8_t ap_startup_try_mark_reported_apic_id(uint8_t apic_id)
{
    uint32_t byte_index = (uint32_t) apic_id >> 3u;
    uint8_t bit_mask = (uint8_t) (1u << (apic_id & 0x7u));

    if ((ap_startup_reported_apic_bitmap[byte_index] & bit_mask) != 0u)
        return 0u;
    ap_startup_reported_apic_bitmap[byte_index] |= bit_mask;
    return 1u;
}

static void ap_startup_record_online_event(uint8_t apic_id, uint32_t logical_slot)
{
    cpu_topology_register_discovered_apic_id(apic_id);
    cpu_topology_mark_apic_id_online(apic_id);
    if (ap_startup_try_mark_reported_apic_id(apic_id))
    {
        ++ap_startup_reported_online_count;
        ap_startup_last_reported_apic_id = apic_id;
    }
    application_processor_bootstrap_mark_booted(logical_slot);
}

/**
 * @brief Get kernel CR3 (page directory physical address).
 *
 * Used by APs to set up the same paging context as BSP.
 */
uint32_t application_processor_startup_get_kernel_cr3(void)
{
    if (!kernel_cr3_cached)
        kernel_cr3_cached = (uint32_t)(uintptr_t)&boot_page_directory - KERNEL_VIRTUAL_BASE;

    return kernel_cr3_cached;
}

/**
 * @brief Set serial port for AP telemetry output.
 *
 * Called during BSP initialization to enable AP logging.
 *
 * @param serial_port Pointer to COM1 serial port struct.
 */
void application_processor_startup_set_serial_port(Serial_t *serial_port)
{
    ap_serial_port = serial_port;
}

uint8_t application_processor_startup_ensure_low_identity_mapping(void)
{
    uint32_t *page_directory = (uint32_t *) (uintptr_t) &boot_page_directory;
    uint32_t page_tables_phys =
        ((uint32_t) (uintptr_t) &boot_page_tables - KERNEL_VIRTUAL_BASE) & 0xFFFFF000u;

    if ((page_directory[0] & 0x1u) == 0u)
    {
        page_directory[0] = page_tables_phys | 0x003u;
        paging_flush_tlb();
    }

    return (uint8_t) ((page_directory[0] & 0x1u) != 0u);
}

/**
 * @brief Initialize AP-specific CPU state after mode transition.
 *
 * Called by AP after jumping to protected mode.
 * Sets up APIC ID, domain affinity, and signals BSP.
 *
 * @param apic_id Physical APIC ID of this AP.
 * @param logical_slot Compacted logical slot for domain binding.
 * @param stack_top Virtual address of stack top for this AP.
 */
void application_processor_startup_initialize_cpu(uint8_t apic_id, uint32_t logical_slot, void *stack_top)
{
    /* Store local context */
    ap_local_context.apic_id = apic_id;
    ap_local_context.logical_slot = logical_slot;
    ap_local_context.initialized = 1u;

    /* GDT/IDT were already loaded in trampoline protected-mode path. */

    /* Set CR3 to BSP page directory (memory is shared in boot) */
    uint32_t cr3 = application_processor_startup_get_kernel_cr3();
    asm volatile("movl %0, %%cr3" : : "r"(cr3));

    /* Enable Local APIC on this CPU (handles x2APIC transition if supported) */
    /* Map mapping for xAPIC MMIO is already handled by BSP in late_init */
    extern uint32_t advanced_pic_timer_backend_get_local_apic_virtual_base(void);
    apic_initialize_on_cpu(advanced_pic_timer_backend_get_local_apic_virtual_base());

    (void) stack_top;

    /* Map AP APIC ID in topology and update startup telemetry. */
    ap_startup_record_online_event(apic_id, logical_slot);

    /* Bind AP to domain (using slot → domain mapping; AP will share BSP domain by default) */
    uint32_t domain = cpu_topology_get_slot_domain(logical_slot);
    
    /* Initialize AP memory domain caching infrastructure */
    kernel_heap_initialize_ap_domain(logical_slot);

    /* Telemetry */
    if (ap_serial_port)
    {
        serial_write_string(ap_serial_port, "[AP ");
        serial_write_hex8(ap_serial_port, apic_id);
        serial_write_string(ap_serial_port, "]: initialized at slot=");
        serial_write_int(ap_serial_port, (int32_t) logical_slot);
        serial_write_string(ap_serial_port, ", domain=");
        serial_write_int(ap_serial_port, (int32_t) domain);
        serial_write_string(ap_serial_port, "\n");
    }

}

void application_processor_startup_report_bootstrap_ack(uint8_t apic_id, uint32_t logical_slot)
{
    ap_startup_record_online_event(apic_id, logical_slot);
}

uint32_t application_processor_startup_get_reported_online_count(void)
{
    return ap_startup_reported_online_count;
}

uint8_t application_processor_startup_get_last_reported_apic_id(void)
{
    return ap_startup_last_reported_apic_id;
}

/**
 * @brief Get AP's local APIC ID.
 */
uint8_t application_processor_startup_get_apic_id(void)
{
    return ap_local_context.apic_id;
}

/**
 * @brief Get AP's logical slot.
 */
uint32_t application_processor_startup_get_logical_slot(void)
{
    return ap_local_context.logical_slot;
}

/**
 * @brief Check if AP CPU has been initialized.
 */
uint8_t application_processor_startup_is_initialized(void)
{
    return ap_local_context.initialized;
}

/**
 * @brief Minimal AP main loop (placeholder for real task scheduling).
 *
 * This is called after AP has transitioned to protected mode and set up registers.
 * For now, just spin waiting for IPI or BSP commands.
 */
void application_processor_startup_main_loop(void)
{
    /* Placeholder: AP waits for work (real scheduler would be here) */
    asm volatile("sti");
    while (1)
    {
        asm volatile("hlt");  /* Wait for interrupt */
    }
}
