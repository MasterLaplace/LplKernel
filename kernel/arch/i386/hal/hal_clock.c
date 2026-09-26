#include <kernel/hal/hal.h>

#include <kernel/cpu/clock.h>
#include <kernel/lib/asmutils.h>

uint32_t hardware_abstraction_layer_clock_tick_count(void) { return clock_get_tick_count(); }

uint32_t hardware_abstraction_layer_clock_tick_hertz(void) { return clock_get_tick_hz(); }

uint64_t hardware_abstraction_layer_clock_timestamp_counter(void) { return asmutils_read_timestamp_counter(); }
