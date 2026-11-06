#include <kernel/cpu/pit.h>

#define PIT_INPUT_FREQUENCY_HZ 1193182u
#define PIT_COMMAND_PORT       0x43u
#define PIT_CHANNEL0_DATA_PORT 0x40u

#define PIT_CHANNEL0_LOHIBYTE_MODE2_BINARY 0x34u

#define PIT_CHANNEL2_DATA_PORT             0x42u   /**< Channel 2's count. */
#define PIT_SYSTEM_CONTROL_PORT_B          0x61u   /**< Gates channel 2, sends it to the speaker, reads its output. */
#define PIT_CHANNEL2_GATE                  0x01u   /**< Port B bit 0: channel 2 counts. */
#define PIT_SPEAKER_DATA                   0x02u   /**< Port B bit 1: channel 2's output drives the speaker. */
#define PIT_CHANNEL2_OUTPUT                0x20u   /**< Port B bit 5, read: channel 2's output. */
#define PIT_CHANNEL2_LOHIBYTE_MODE0_BINARY 0xB0u   /**< Channel 2, low then high byte, mode 0, binary. */
#define PIT_CHANNEL2_LATCH                 0x80u   /**< Channel 2's count latched for the next two reads. */
#define PIT_CHANNEL2_FULL_COUNT            0xFFFFu /**< What channel 2 counts down from: 54.9 ms. */

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

    asmutils_output_byte(PIT_COMMAND_PORT, PIT_CHANNEL0_LOHIBYTE_MODE2_BINARY);
    asmutils_output_byte(PIT_CHANNEL0_DATA_PORT, (uint8_t) (divisor & 0x00FFu));
    asmutils_output_byte(PIT_CHANNEL0_DATA_PORT, (uint8_t) ((divisor >> 8u) & 0x00FFu));

    programmable_interval_timer_frequency_hz = PIT_INPUT_FREQUENCY_HZ / divisor;
}

uint32_t programmable_interval_timer_get_frequency_hz(void) { return programmable_interval_timer_frequency_hz; }

uint32_t programmable_interval_timer_start_channel_2(void)
{
    const uint8_t port_b = asmutils_input_byte(PIT_SYSTEM_CONTROL_PORT_B);
    uint32_t loaded;

    asmutils_output_byte(PIT_SYSTEM_CONTROL_PORT_B, (uint8_t) ((port_b & ~PIT_SPEAKER_DATA) | PIT_CHANNEL2_GATE));
    asmutils_output_byte(PIT_COMMAND_PORT, PIT_CHANNEL2_LOHIBYTE_MODE0_BINARY);
    asmutils_output_byte(PIT_CHANNEL2_DATA_PORT, (uint8_t) (PIT_CHANNEL2_FULL_COUNT & 0x00FFu));
    asmutils_output_byte(PIT_CHANNEL2_DATA_PORT, (uint8_t) ((PIT_CHANNEL2_FULL_COUNT >> 8u) & 0x00FFu));
    (void) programmable_interval_timer_read_channel_2(&loaded);
    return PIT_INPUT_FREQUENCY_HZ;
}

bool programmable_interval_timer_read_channel_2(uint32_t *out_counted)
{
    asmutils_output_byte(PIT_COMMAND_PORT, PIT_CHANNEL2_LATCH);

    const uint32_t low = asmutils_input_byte(PIT_CHANNEL2_DATA_PORT);
    const uint32_t high = asmutils_input_byte(PIT_CHANNEL2_DATA_PORT);

    *out_counted = PIT_CHANNEL2_FULL_COUNT - (low | (high << 8u));
    return (asmutils_input_byte(PIT_SYSTEM_CONTROL_PORT_B) & PIT_CHANNEL2_OUTPUT) == 0u;
}
