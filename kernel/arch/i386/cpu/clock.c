#include <kernel/cpu/clock.h>

#include <kernel/cpu/irq.h>

#define CLOCK_BACKEND_NAME_PIT "pit-irq0"

#define CLOCK_CLIENT_TIMER_FREQUENCY_HZ 100u
#define CLOCK_SERVER_TIMER_FREQUENCY_HZ 100u

static ClockProfile_t clock_profile = CLOCK_PROFILE_SERVER_THROUGHPUT;
static const char *clock_backend_name = CLOCK_BACKEND_NAME_PIT;

void clock_initialize(void)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    clock_profile = CLOCK_PROFILE_CLIENT_RT;
    interrupt_request_set_timer_frequency_hz(CLOCK_CLIENT_TIMER_FREQUENCY_HZ);
    interrupt_request_set_realtime_clock_periodic_enabled(0u);
#else
    clock_profile = CLOCK_PROFILE_SERVER_THROUGHPUT;
    interrupt_request_set_timer_frequency_hz(CLOCK_SERVER_TIMER_FREQUENCY_HZ);
    interrupt_request_set_realtime_clock_periodic_enabled(0u);
#endif

    clock_backend_name = CLOCK_BACKEND_NAME_PIT;
    interrupt_request_initialize();
}

ClockProfile_t clock_get_profile(void) { return clock_profile; }

const char *clock_get_backend_name(void) { return clock_backend_name; }

const char *clock_get_profile_name(void)
{
    if (clock_profile == CLOCK_PROFILE_CLIENT_RT)
        return "client-rt";
    return "server-throughput";
}

uint32_t clock_get_tick_hz(void) { return interrupt_request_get_timer_frequency_hz(); }

uint32_t clock_get_tick_count(void) { return interrupt_request_get_tick_count(); }

uint32_t clock_get_spurious_irq7_count(void) { return interrupt_request_get_spurious_irq7_count(); }

uint32_t clock_get_spurious_irq15_count(void) { return interrupt_request_get_spurious_irq15_count(); }

uint32_t clock_get_rtc_periodic_interrupt_count(void) { return interrupt_request_get_realtime_clock_interrupt_count(); }

uint8_t clock_is_rtc_periodic_enabled(void) { return interrupt_request_is_realtime_clock_periodic_enabled(); }

void clock_read_walltime(RealtimeClockTime_t *time_snapshot) { realtime_clock_read_time(time_snapshot); }
