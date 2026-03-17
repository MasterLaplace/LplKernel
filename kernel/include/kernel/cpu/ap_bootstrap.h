/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** AP bootstrap stack and initialization header
*/

#ifndef KERNEL_CPU_AP_BOOTSTRAP_H_
#define KERNEL_CPU_AP_BOOTSTRAP_H_

#include <stdint.h>

/**
 * @brief Bootstrap entry for an Application Processor (AP).
 *
 * Contains APIC ID, logical slot mapping, and stack allocation info.
 */
typedef struct APBootstrapEntry {
    uint8_t apic_id;          /* APIC ID of this AP */
    uint32_t logical_slot;    /* Compacted logical slot from topology */
    void *stack_base;         /* Virtual address of stack base */
    uint32_t stack_size;      /* Stack size (typically 8 KB) */
    uint8_t initialized;      /* Whether this entry is valid */
    uint8_t booted;           /* Whether AP has been started */
} ApplicationProcessorBootstrapEntry_t;

/**
 * @brief Table of AP bootstrap entries, along with count and iteration cursor.
 *
 * Indexed by logical slot (0-31), with one entry per possible AP (excluding BSP).
 * Includes count of valid AP entries and cursor for iterating through unbooted APs.
 * @note Logical slot is a compacted index assigned during topology discovery, not necessarily contiguous.
 */
typedef struct APBootstrapTable_t {
    ApplicationProcessorBootstrapEntry_t entries[32u];  /* One per possible slot */
    uint32_t ap_count;                 /* Number of non-BSP APs */
    uint32_t next_entry_index;         /* Cursor for iteration */
} ApplicationProcessorBootstrapTable_t;

/**
 * @brief Initialize AP bootstrap table and allocate per-AP stacks.
 *
 * Call after:
 * - CPU topology discovery complete (APIC IDs registered)
 * - Heap operational
 * - Paging ready
 *
 * Allocates 8 KB stack per non-BSP AP discovered in topology.
 * Stores virtual addresses for use in AP protected mode entry.
 *
 * @return Non-zero on success; zero if heap allocation fails.
 */
extern uint8_t application_processor_bootstrap_initialize(void);

/**
 * @brief Get AP bootstrap entry by APIC ID.
 * @return Pointer to entry; NULL if APIC ID not found.
 */
extern ApplicationProcessorBootstrapEntry_t *application_processor_bootstrap_get_entry_by_apic_id(uint8_t apic_id);

/**
 * @brief Get AP bootstrap entry by logical CPU slot.
 * @return Pointer to entry; NULL if slot not valid or not initialized.
 */
extern ApplicationProcessorBootstrapEntry_t *application_processor_bootstrap_get_entry_by_slot(uint32_t logical_slot);

/**
 * @brief Get total count of non-BSP APs discovered.
 */
extern uint32_t application_processor_bootstrap_get_ap_count(void);

/**
 * @brief Get kernel virtual address of AP stack top (RSP initial value).
 * @return Virtual address; NULL if slot invalid.
 */
extern void *application_processor_bootstrap_get_ap_stack_top(uint32_t logical_slot);

/**
 * @brief Get physical address of AP stack top (for real mode bootstrap).
 * @return Physical address; 0 if invalid.
 */
extern uint32_t application_processor_bootstrap_get_ap_stack_phys(uint32_t logical_slot);

/**
 * @brief Mark an AP as successfully booted.
 */
extern void application_processor_bootstrap_mark_booted(uint32_t logical_slot);

/**
 * @brief Check if an AP has been booted.
 * @return Non-zero if booted; zero otherwise.
 */
extern uint8_t application_processor_bootstrap_is_booted(uint32_t logical_slot);

/**
 * @brief Reset iteration cursor for walking unbooted APs.
 */
extern void application_processor_bootstrap_reset_iteration(void);

/**
 * @brief Get next unbooted AP entry; NULL when exhausted.
 *
 * Use with application_processor_bootstrap_reset_iteration() for a full pass.
 */
extern ApplicationProcessorBootstrapEntry_t *application_processor_bootstrap_next_unbooted_ap(void);

#endif /* !KERNEL_CPU_AP_BOOTSTRAP_H_ */
