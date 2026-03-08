/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** clock
*/

#ifndef CLOCK_H_
#define CLOCK_H_

#include <kernel/drivers/rtc.h>

#include <stdint.h>

typedef enum {
    CLOCK_PROFILE_CLIENT_RT = 0,
    CLOCK_PROFILE_SERVER_THROUGHPUT = 1,
} ClockProfile_t;

/**
 * @brief Initialize timer subsystem using active build profile policy.
 */
extern void clock_initialize(void);

/**
 * @brief Return selected runtime clock profile.
 */
extern ClockProfile_t clock_get_profile(void);

/**
 * @brief Return active timer backend name.
 */
extern const char *clock_get_backend_name(void);

/**
 * @brief Return active profile name.
 */
extern const char *clock_get_profile_name(void);

/**
 * @brief Return current scheduler tick frequency in Hertz.
 */
extern uint32_t clock_get_tick_hz(void);

/**
 * @brief Return number of tick interrupts since clock initialization.
 */
extern uint32_t clock_get_tick_count(void);

/**
 * @brief Return detected spurious IRQ7 count.
 */
extern uint32_t clock_get_spurious_irq7_count(void);

/**
 * @brief Return detected spurious IRQ15 count.
 */
extern uint32_t clock_get_spurious_irq15_count(void);

/**
 * @brief Return periodic RTC IRQ count (0 when disabled).
 */
extern uint32_t clock_get_rtc_periodic_interrupt_count(void);

/**
 * @brief Return non-zero when periodic RTC IRQ mode is enabled.
 */
extern uint8_t clock_is_rtc_periodic_enabled(void);

/**
 * @brief Read RTC wall-clock snapshot via polling (no periodic IRQ required).
 */
extern void clock_read_walltime(RealtimeClockTime_t *time_snapshot);

#endif /* !CLOCK_H_ */
