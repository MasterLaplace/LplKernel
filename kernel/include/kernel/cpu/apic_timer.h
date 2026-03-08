/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** apic_timer
*/

#ifndef APIC_TIMER_H_
#define APIC_TIMER_H_

#include <stdint.h>

/**
 * @brief Initialize advanced PIC timer backend (probe-only in current stage).
 *
 * @return Non-zero when advanced PIC timer backend is active, zero when unavailable.
 */
extern uint8_t advanced_pic_timer_backend_initialize(uint32_t target_frequency_hz);

/**
 * @brief Perform late APIC runtime preparation after paging/PMM init.
 *
 * This stage maps LAPIC MMIO, validates register accessibility, and keeps
 * APIC timer LVT masked (no scheduler ownership takeover yet).
 *
 * @return Non-zero when late preparation succeeded, zero otherwise.
 */
extern uint8_t advanced_pic_timer_backend_late_initialize(void);

/**
 * @brief Return advanced PIC backend name string.
 */
extern const char *advanced_pic_timer_backend_name(void);

/**
 * @brief Return detected local APIC physical MMIO base address.
 *
 * Returns 0 when probe failed or local APIC MMIO mode is unavailable.
 */
extern uint32_t advanced_pic_timer_backend_get_local_apic_physical_base(void);

/**
 * @brief Return non-zero when LAPIC MMIO page is mapped for runtime access.
 */
extern uint8_t advanced_pic_timer_backend_is_local_apic_mmio_mapped(void);

/**
 * @brief Return LAPIC version register value observed during late initialization.
 */
extern uint32_t advanced_pic_timer_backend_get_local_apic_version_register(void);

/**
 * @brief Calibrate LAPIC timer rate against PIT tick cadence.
 *
 * This calibration keeps LAPIC timer masked and only produces diagnostics for
 * next-stage APIC timer ownership migration.
 *
 * @return Non-zero when calibration data is available, zero otherwise.
 */
extern uint8_t advanced_pic_timer_backend_calibrate_with_pit(void);

/**
 * @brief Return measured LAPIC timer frequency estimate in Hertz.
 */
extern uint32_t advanced_pic_timer_backend_get_calibrated_timer_frequency_hz(void);

/**
 * @brief Return whether current CPU is bootstrap processor according to APIC base MSR.
 */
extern uint8_t advanced_pic_timer_backend_is_bootstrap_processor(void);

#endif /* !APIC_TIMER_H_ */
