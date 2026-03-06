/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** rtc
*/

#ifndef RTC_H_
#define RTC_H_

#include <stdint.h>

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} RealtimeClockTime_t;

/**
 * @brief Initialize RTC support (handler registration + known register state).
 */
extern void realtime_clock_initialize(void);

/**
 * @brief Enable/disable periodic IRQ8 generation.
 */
extern void realtime_clock_set_periodic_interrupt_enabled(uint8_t enabled);

/**
 * @brief Return non-zero when periodic IRQ8 generation is enabled.
 */
extern uint8_t realtime_clock_is_periodic_interrupt_enabled(void);

/**
 * @brief Return number of handled periodic RTC IRQ8 events.
 */
extern uint32_t realtime_clock_get_periodic_interrupt_count(void);

/**
 * @brief Read a coherent RTC wall-clock snapshot from CMOS.
 */
extern void realtime_clock_read_time(RealtimeClockTime_t *time_snapshot);

#endif /* !RTC_H_ */
