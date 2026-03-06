#include <kernel/drivers/rtc.h>

#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/lib/asmutils.h>

#include <stddef.h>

#define CMOS_INDEX_PORT 0x70u
#define CMOS_DATA_PORT  0x71u

#define RTC_REGISTER_A 0x0Au
#define RTC_REGISTER_B 0x0Bu
#define RTC_REGISTER_C 0x0Cu

#define RTC_REG_A_UPDATE_IN_PROGRESS 0x80u
#define RTC_REG_A_RATE_MASK          0x0Fu

#define RTC_REG_B_24HOUR_MODE 0x02u
#define RTC_REG_B_BINARY_MODE 0x04u
#define RTC_REG_B_PERIODIC_IRQ 0x40u

#define RTC_PERIODIC_RATE_64HZ 0x0Au

#define IRQ_RTC_LINE   8u
#define IRQ_RTC_VECTOR (PIC_VECTOR_OFFSET_SLAVE + 0u)

static volatile uint32_t realtime_clock_periodic_interrupt_count = 0u;
static uint8_t realtime_clock_periodic_interrupt_enabled = 0u;

static uint8_t realtime_clock_read_register(uint8_t reg)
{
    outb((short) CMOS_INDEX_PORT, (unsigned char) reg);
    return (uint8_t) inb((short) CMOS_DATA_PORT);
}

static void realtime_clock_write_register(uint8_t reg, uint8_t value)
{
    outb((short) CMOS_INDEX_PORT, (unsigned char) reg);
    outb((short) CMOS_DATA_PORT, (unsigned char) value);
}

static uint8_t realtime_clock_bcd_to_binary(uint8_t value)
{
    return (uint8_t) ((value & 0x0Fu) + ((value >> 4u) * 10u));
}

static uint8_t realtime_clock_is_update_in_progress(void)
{
    return (uint8_t) ((realtime_clock_read_register(RTC_REGISTER_A) & RTC_REG_A_UPDATE_IN_PROGRESS) != 0u);
}

static void realtime_clock_read_raw(uint8_t *second, uint8_t *minute, uint8_t *hour, uint8_t *day, uint8_t *month,
                                    uint8_t *year, uint8_t *register_b)
{
    *second = realtime_clock_read_register(0x00u);
    *minute = realtime_clock_read_register(0x02u);
    *hour = realtime_clock_read_register(0x04u);
    *day = realtime_clock_read_register(0x07u);
    *month = realtime_clock_read_register(0x08u);
    *year = realtime_clock_read_register(0x09u);
    *register_b = realtime_clock_read_register(RTC_REGISTER_B);
}

static void realtime_clock_interrupt_handler(const InterruptFrame_t *frame)
{
    (void) frame;

    realtime_clock_periodic_interrupt_count++;
    (void) realtime_clock_read_register(RTC_REGISTER_C);
    programmable_interrupt_controller_send_end_of_interrupt(IRQ_RTC_LINE);
}

void realtime_clock_initialize(void)
{
    uint8_t register_b;

    interrupt_service_routine_register_handler(IRQ_RTC_VECTOR, realtime_clock_interrupt_handler);

    register_b = realtime_clock_read_register(RTC_REGISTER_B);
    register_b &= (uint8_t) ~RTC_REG_B_PERIODIC_IRQ;
    realtime_clock_write_register(RTC_REGISTER_B, register_b);

    realtime_clock_periodic_interrupt_enabled = 0u;
    realtime_clock_periodic_interrupt_count = 0u;
    (void) realtime_clock_read_register(RTC_REGISTER_C);
}

void realtime_clock_set_periodic_interrupt_enabled(uint8_t enabled)
{
    uint8_t register_a;
    uint8_t register_b;

    register_a = realtime_clock_read_register(RTC_REGISTER_A);
    register_a = (uint8_t) ((register_a & (uint8_t) ~RTC_REG_A_RATE_MASK) | RTC_PERIODIC_RATE_64HZ);
    realtime_clock_write_register(RTC_REGISTER_A, register_a);

    register_b = realtime_clock_read_register(RTC_REGISTER_B);
    if (enabled)
        register_b |= RTC_REG_B_PERIODIC_IRQ;
    else
        register_b &= (uint8_t) ~RTC_REG_B_PERIODIC_IRQ;
    realtime_clock_write_register(RTC_REGISTER_B, register_b);

    realtime_clock_periodic_interrupt_enabled = (uint8_t) (enabled != 0u);
    (void) realtime_clock_read_register(RTC_REGISTER_C);
}

uint8_t realtime_clock_is_periodic_interrupt_enabled(void)
{
    return realtime_clock_periodic_interrupt_enabled;
}

uint32_t realtime_clock_get_periodic_interrupt_count(void)
{
    return realtime_clock_periodic_interrupt_count;
}

void realtime_clock_read_time(RealtimeClockTime_t *time_snapshot)
{
    uint8_t second_1;
    uint8_t minute_1;
    uint8_t hour_1;
    uint8_t day_1;
    uint8_t month_1;
    uint8_t year_1;
    uint8_t register_b_1;
    uint8_t second_2;
    uint8_t minute_2;
    uint8_t hour_2;
    uint8_t day_2;
    uint8_t month_2;
    uint8_t year_2;
    uint8_t register_b_2;

    if (!time_snapshot)
        return;

    do
    {
        while (realtime_clock_is_update_in_progress())
        {
        }
        realtime_clock_read_raw(&second_1, &minute_1, &hour_1, &day_1, &month_1, &year_1, &register_b_1);

        while (realtime_clock_is_update_in_progress())
        {
        }
        realtime_clock_read_raw(&second_2, &minute_2, &hour_2, &day_2, &month_2, &year_2, &register_b_2);
    } while (second_1 != second_2 || minute_1 != minute_2 || hour_1 != hour_2 || day_1 != day_2 ||
             month_1 != month_2 || year_1 != year_2);

    if ((register_b_2 & RTC_REG_B_BINARY_MODE) == 0u)
    {
        second_2 = realtime_clock_bcd_to_binary(second_2);
        minute_2 = realtime_clock_bcd_to_binary(minute_2);
        day_2 = realtime_clock_bcd_to_binary(day_2);
        month_2 = realtime_clock_bcd_to_binary(month_2);
        year_2 = realtime_clock_bcd_to_binary(year_2);

        hour_2 = (uint8_t) ((hour_2 & 0x80u) | realtime_clock_bcd_to_binary((uint8_t) (hour_2 & 0x7Fu)));
    }

    if ((register_b_2 & RTC_REG_B_24HOUR_MODE) == 0u)
    {
        uint8_t is_pm = (uint8_t) ((hour_2 & 0x80u) != 0u);

        hour_2 &= 0x7Fu;
        if (is_pm && hour_2 < 12u)
            hour_2 = (uint8_t) (hour_2 + 12u);
        if (!is_pm && hour_2 == 12u)
            hour_2 = 0u;
    }
    else
        hour_2 &= 0x7Fu;

    time_snapshot->second = second_2;
    time_snapshot->minute = minute_2;
    time_snapshot->hour = hour_2;
    time_snapshot->day = day_2;
    time_snapshot->month = month_2;
    time_snapshot->year = (uint16_t) (2000u + year_2);
}
