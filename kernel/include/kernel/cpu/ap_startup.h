/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** AP startup initialization header
*/

#ifndef KERNEL_CPU_AP_STARTUP_H_
#define KERNEL_CPU_AP_STARTUP_H_

#include <kernel/drivers/serial.h>
#include <stdint.h>

/**
 * @brief Local CPU context for an Application Processor (AP) during startup.
 *
 * Stores APIC ID, logical slot, and initialization status for the AP.
 * This is used by the AP during its startup sequence to identify itself and
 * report back to the BSP.
 */
typedef struct APLocalContext {
    uint8_t apic_id;
    uint32_t logical_slot;
    uint8_t initialized;
} ApplicationProcessorLocalContext_t;

/**
 * @brief Get kernel CR3 (page directory physical address).
 *
 * Used by APs to set up the same paging context as BSP.
 *
 * @return Physical address of kernel page directory.
 */
extern uint32_t application_processor_startup_get_kernel_cr3(void);

/**
 * @brief Set serial port for AP telemetry output.
 *
 * Must be called during BSP initialization to enable AP logging.
 *
 * @param serial_port Pointer to COM1 serial port struct.
 */
extern void application_processor_startup_set_serial_port(Serial_t *serial_port);

/**
 * @brief Ensure low identity mapping is present in kernel CR3 for AP bootstrap.
 *
 * AP trampoline executes in low physical memory before jumping to higher-half
 * mapped code. This helper restores PDE[0] mapping when it was cleared.
 *
 * @return Non-zero when identity mapping is available after the call.
 */
extern uint8_t application_processor_startup_ensure_low_identity_mapping(void);

/**
 * @brief Initialize AP CPU after protected mode transition.
 *
 * Called by AP after real→protected mode jump.
 * Sets up GDT, IDT, paging, APIC ID registration, and domain affinity.
 *
 * @param apic_id Physical APIC ID of this AP.
 * @param logical_slot Compacted logical CPU slot from topology.
 * @param stack_top Virtual address of AP's stack top.
 */
extern void application_processor_startup_initialize_cpu(uint8_t apic_id, uint32_t logical_slot, void *stack_top);

/**
 * @brief Record a successful AP bootstrap acknowledgement.
 *
 * Used by BSP after INIT/SIPI + trampoline acknowledgement while full AP
 * protected-mode handoff is still being integrated.
 *
 * @param apic_id Physical APIC ID of this AP.
 * @param logical_slot Compacted logical CPU slot from topology.
 */
extern void application_processor_startup_report_bootstrap_ack(uint8_t apic_id, uint32_t logical_slot);

/**
 * @brief Return number of APs that reported online via startup initialization.
 */
extern uint32_t application_processor_startup_get_reported_online_count(void);

/**
 * @brief Return last APIC ID reported online during AP startup.
 */
extern uint8_t application_processor_startup_get_last_reported_apic_id(void);

/**
 * @brief Get this AP's APIC ID.
 * @return Physical APIC ID; 0xFF if not initialized.
 */
extern uint8_t application_processor_startup_get_apic_id(void);

/**
 * @brief Get this AP's logical CPU slot.
 * @return Logical slot; 0 if not initialized.
 */
extern uint32_t application_processor_startup_get_logical_slot(void);

/**
 * @brief Check if AP initialization is complete.
 * @return Non-zero if AP has been initialized.
 */
extern uint8_t application_processor_startup_is_initialized(void);

/**
 * @brief Main event loop for AP (after initialization).
 *
 * Placeholder for AP-specific tasks. Currently spins with HLT.
 * In a full scheduler, this would wait for work or interrupts.
 */
extern void application_processor_startup_main_loop(void);

#endif /* !KERNEL_CPU_AP_STARTUP_H_ */
