/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** irq
*/

#ifndef IRQ_H_
#define IRQ_H_

#include <stdint.h>

/**
 * @brief Initialize IRQ runtime for current bring-up stage.
 *
 * This routine remaps the PIC, masks all IRQ lines, installs the IRQ0
 * timer handler, initializes keyboard IRQ1 and RTC support, then enables
 * selected lines and interrupts globally.
 */
extern void interrupt_request_initialize(void);

/**
 * @brief Configure IRQ0 timer target frequency for next initialization.
 */
extern void interrupt_request_set_timer_frequency_hz(uint32_t frequency_hz);

/**
 * @brief Configure RTC periodic IRQ8 policy for next initialization.
 */
extern void interrupt_request_set_realtime_clock_periodic_enabled(uint8_t enabled);

/**
 * @brief Return IRQ0 tick count since IRQ initialization.
 */
extern uint32_t interrupt_request_get_tick_count(void);

/**
 * @brief Return configured PIT frequency used by IRQ0 in Hertz.
 */
extern uint32_t interrupt_request_get_timer_frequency_hz(void);

/**
 * @brief Return number of detected spurious IRQ7 events.
 */
extern uint32_t interrupt_request_get_spurious_irq7_count(void);

/**
 * @brief Return number of detected spurious IRQ15 events.
 */
extern uint32_t interrupt_request_get_spurious_irq15_count(void);

/**
 * @brief Return number of handled periodic RTC IRQ8 events.
 */
extern uint32_t interrupt_request_get_realtime_clock_interrupt_count(void);

/**
 * @brief Return non-zero when periodic RTC IRQ8 mode is enabled.
 */
extern uint8_t interrupt_request_is_realtime_clock_periodic_enabled(void);

#endif /* !IRQ_H_ */
