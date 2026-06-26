/**
 * @file hal_clock.c
 * @brief Tick / timestamp backend for the engine HAL.
 *
 * Implements the hal_clock_* contract over the kernel clock driver, plus a
 * 64-bit rdtsc reader for sub-tick timing. Tick deltas are intended to be
 * consumed modulo 2^32 (the counter wraps); the timestamp counter is for
 * fine-grained, non-authoritative timing only (it is non-deterministic and
 * therefore never feeds the Fixed32 simulation authority).
 */
#include <kernel/hal/hal.h>

#include <kernel/cpu/clock.h>

uint32_t hal_clock_tick_count(void) { return clock_get_tick_count(); }

uint32_t hal_clock_tick_hertz(void) { return clock_get_tick_hz(); }

uint64_t hal_clock_timestamp_counter(void)
{
    uint32_t low = 0u;
    uint32_t high = 0u;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t) high << 32) | (uint64_t) low;
}
