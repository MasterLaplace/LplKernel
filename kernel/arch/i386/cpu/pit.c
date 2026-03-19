#include <kernel/cpu/pit.h>

#include <kernel/lib/asmutils.h>

#define PIT_INPUT_FREQUENCY_HZ 1193182u
#define PIT_COMMAND_PORT       0x43u
#define PIT_CHANNEL0_DATA_PORT 0x40u

#define PIT_CHANNEL0_LOHIBYTE_MODE2_BINARY 0x34u

static uint32_t programmable_interval_timer_frequency_hz = 0u;

static uint16_t programmable_interval_timer_frequency_to_divisor(uint32_t target_frequency_hz)
{
    uint32_t divisor;

    if (target_frequency_hz == 0u)
        target_frequency_hz = 1u;

    divisor = PIT_INPUT_FREQUENCY_HZ / target_frequency_hz;

    if (divisor == 0u)
        divisor = 1u;
    if (divisor > 0xFFFFu)
        divisor = 0xFFFFu;

    return (uint16_t) divisor;
}

void programmable_interval_timer_initialize(uint32_t target_frequency_hz)
{
    uint16_t divisor = programmable_interval_timer_frequency_to_divisor(target_frequency_hz);

    asmutils_output_byte((short) PIT_COMMAND_PORT, PIT_CHANNEL0_LOHIBYTE_MODE2_BINARY);
    asmutils_output_byte((short) PIT_CHANNEL0_DATA_PORT, (unsigned char) (divisor & 0x00FFu));
    asmutils_output_byte((short) PIT_CHANNEL0_DATA_PORT, (unsigned char) ((divisor >> 8u) & 0x00FFu));

    programmable_interval_timer_frequency_hz = PIT_INPUT_FREQUENCY_HZ / divisor;
}

uint32_t programmable_interval_timer_get_frequency_hz(void) { return programmable_interval_timer_frequency_hz; }
