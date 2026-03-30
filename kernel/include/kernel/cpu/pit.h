/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** pit
*/

#ifndef KERNEL_CPU_PROGRAMMABLE_INTERVAL_TIMER_H
#define KERNEL_CPU_PROGRAMMABLE_INTERVAL_TIMER_H

#include <kernel/lib/asmutils.h>

#include <stdint.h>

/**
 * @brief Configure PIT channel 0 in rate generator mode.
 *
 * @param target_frequency_hz Requested IRQ0 frequency in Hertz.
 */
extern void programmable_interval_timer_initialize(uint32_t target_frequency_hz);

/**
 * @brief Return currently programmed PIT channel 0 frequency in Hertz.
 */
extern uint32_t programmable_interval_timer_get_frequency_hz(void);

#endif /* KERNEL_CPU_PROGRAMMABLE_INTERVAL_TIMER_H */
